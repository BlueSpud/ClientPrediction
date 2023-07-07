#include "World/ClientPredictionReplicationManager.h"

#include "Net/UnrealNetwork.h"
#include "World/ClientPredictionWorldManager.h"

// Serialization
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetSerializeSnapshot(FArchive& Ar, UPackageMap* Map, FModelSnapshot& Snapshot) {
    Snapshot.ModelId.NetSerialize(Ar, Map);

    uint16 DataLength;
    if (Ar.IsSaving()) {
        DataLength = static_cast<uint16>(Snapshot.Data.Num());
    }

    Ar << DataLength;

    if (Ar.IsLoading()) {
        Snapshot.Data.SetNumUninitialized(DataLength);
    }

    Ar.Serialize(Snapshot.Data.GetData(), DataLength);
}

void NetSerializeSnapshotArray(FArchive& Ar, UPackageMap* Map, TArray<FModelSnapshot>& Snapshots) {
    uint16 NumSnapshots;
    if (Ar.IsSaving()) {
        NumSnapshots = static_cast<uint16>(Snapshots.Num());
    }

    Ar << NumSnapshots;

    if (Ar.IsLoading()) {
        Snapshots.SetNum(NumSnapshots);
    }

    for (uint16 i = 0; i < NumSnapshots; ++i) {
        NetSerializeSnapshot(Ar, Map, Snapshots[i]);
    }
}

bool FReplicatedTickSnapshot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    Ar << TickNumber;
    NetSerializeSnapshotArray(Ar, Map, SimProxyModels);
    NetSerializeSnapshotArray(Ar, Map, AutoProxyModels);

    bOutSuccess = true;
    return true;
}

bool FReplicatedTickSnapshot::Identical(const FReplicatedTickSnapshot* Other, uint32 PortFlags) const {
    return TickNumber == Other->TickNumber;
}

// Replication manager
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AClientPredictionReplicationManager::AClientPredictionReplicationManager() : Settings(GetDefault<UClientPredictionSettings>()) {
    PrimaryActorTick.bCanEverTick = false;
    bAlwaysRelevant = true;
    bReplicates = true;

    SetReplicateMovement(false);
}

void AClientPredictionReplicationManager::PostActorCreated() {
    Super::PostActorCreated();
    CachedOwner = GetOwner();
}

bool AClientPredictionReplicationManager::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const {
    return RealViewer == CachedOwner;
}

void AClientPredictionReplicationManager::PostNetInit() {
    Super::PostNetInit();

    ClientPrediction::FWorldManager* Manager = ClientPrediction::FWorldManager::ManagerForWorld(GetWorld());
    if (Manager == nullptr) { return; }

    Manager->RegisterLocalReplicationManager(this);
    LastWorldTime = GetWorld()->GetRealTimeSeconds();
}

void AClientPredictionReplicationManager::PostTickAuthority(int32 TickNumber) {
    if (StateManager == nullptr && TickNumber % Settings->SnapshotSendCadence != 0) { return; }

    // If there is no owning player, that player has logged out and this replication manager will be destroyed shortly
    if (Owner == nullptr) { return; }
    const UPlayer* OwningPlayer = Owner->GetNetOwningPlayer();
    if (OwningPlayer == nullptr) { return; }

    ClientPrediction::FTickSnapshot TickSnapshot{};
    StateManager->GetProducedDataForTick(TickNumber, TickSnapshot);

    FReplicatedTickSnapshot NewSnapshot = {};
    NewSnapshot.TickNumber = TickNumber;

    FReplicatedTickSnapshot ReliableSnapshot{};
    ReliableSnapshot.TickNumber = TickNumber;

    for (auto& Pair : TickSnapshot.StateData) {
        const UPlayer* ModelOwner = Pair.Key.MapToOwningPlayer();
        const bool bIsOwnedByCurrentPlayer = ModelOwner == OwningPlayer;

        check(Pair.Value.Data.Contains(ClientPrediction::EDataCompleteness::kStandard))
        check(Pair.Value.Data.Contains(ClientPrediction::EDataCompleteness::kFull))

        TArray<uint8>& StandardData = Pair.Value.Data[ClientPrediction::EDataCompleteness::kStandard];
        TArray<uint8>& FullData = Pair.Value.Data[ClientPrediction::EDataCompleteness::kFull];

        if (Pair.Value.bShouldBeReliable) {
            // Reliable messages should always include the full state because the main reason is to have the events
            if (bIsOwnedByCurrentPlayer) {
                ReliableSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(FullData)});
            }
            else {
                ReliableSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(FullData)});
            }
        }
        else {
            if (bIsOwnedByCurrentPlayer) {
                NewSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(FullData)});
            }
            else {
                NewSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(StandardData)});
            }
        }
    }

    FScopeLock QueuedSnapshotLock(&SendQueueMutex);
    SnapshotSendQueue.Enqueue(MoveTemp(NewSnapshot));

    // If there is data that needs to be reliably sent, it is done so over a reliable RPC
    if (!ReliableSnapshot.AutoProxyModels.IsEmpty() || !ReliableSnapshot.SimProxyModels.IsEmpty()) {
        ReliableSnapshotSendQueue.Enqueue(MoveTemp(ReliableSnapshot));
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadAuthority() {
    FScopeLock QueuedSnapshotLock(&SendQueueMutex);
    while (!SnapshotSendQueue.IsEmpty()) {
        SnapshotReceivedRemote(*SnapshotSendQueue.Peek());
        SnapshotSendQueue.Pop();
    }

    while (!ReliableSnapshotSendQueue.IsEmpty()) {
        SnapshotReceivedReliable(*ReliableSnapshotSendQueue.Peek());
        ReliableSnapshotSendQueue.Pop();
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadRemote() {
    if (StateManager == nullptr) { return; }

    const int32 PreviousLatestReceivedTick = LatestReceivedTick;
    while (!SnapshotReceiveQueue.IsEmpty()) {
        ProcessSnapshot(*SnapshotReceiveQueue.Peek());
        SnapshotReceiveQueue.Pop();
    }

    // Here we request a dilation of time (if the current estimated time is close enough) or adjust it directly if the delta is too large to ensure that we are
    // still in sync with the server. This only actually needs to be done when we've gotten a new tick from the server.
    if (LatestReceivedTick != PreviousLatestReceivedTick) {
        AdjustServerTime();
    }

    const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();
    const Chaos::FReal WorldDt = WorldTime - LastWorldTime;

    ServerTimescale = FMath::Lerp(ServerTimescale, ServerTargetTimescale, Settings->SimProxyTimeDilationAlpha);
    ServerTime += WorldDt * ServerTimescale;
    LastWorldTime = WorldTime;

    const Chaos::FReal InterpolationTime = ServerTime - Settings->SimProxyDelay;
    StateManager->SetInterpolationTime(InterpolationTime);
    StateManager->SetEstimatedCurrentServerTick(EstimateServerTick(InterpolationTime));
}

void AClientPredictionReplicationManager::SnapshotReceivedRemote_Implementation(const FReplicatedTickSnapshot& Snapshot) {
    FScopeLock QueuedSnapshotLock(&ReceiveQueueMutex);
    SnapshotReceiveQueue.Enqueue({Snapshot, ClientPrediction::kStandard});
}

void AClientPredictionReplicationManager::SnapshotReceivedReliable_Implementation(const FReplicatedTickSnapshot& Snapshot) {
    FScopeLock QueuedSnapshotLock(&ReceiveQueueMutex);
    SnapshotReceiveQueue.Enqueue({Snapshot, ClientPrediction::kFull});
}

void AClientPredictionReplicationManager::ProcessSnapshot(const TTuple<FReplicatedTickSnapshot, ClientPrediction::EDataCompleteness>& Tuple) {
    if (StateManager == nullptr) { return; }

    const FReplicatedTickSnapshot& Snapshot = Tuple.Key;
    const ClientPrediction::EDataCompleteness AutoProxyCompleteness = Tuple.Value;

    if (ServerStartTick == INDEX_NONE) {
        const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();

        ServerStartTick = Snapshot.TickNumber;
        ServerStartTime = WorldTime;

        ServerTime = WorldTime;
    }

    const Chaos::FReal SnapshotServerTime = CalculateTimeForServerTick(Snapshot.TickNumber);
    for (const auto& AutoProxy : Snapshot.AutoProxyModels) {
        StateManager->PushStateToConsumer(Snapshot.TickNumber, AutoProxy.ModelId, AutoProxy.Data, SnapshotServerTime, ClientPrediction::kFull);
    }

    for (const auto& SimProxy : Snapshot.SimProxyModels) {
        StateManager->PushStateToConsumer(Snapshot.TickNumber, SimProxy.ModelId, SimProxy.Data, SnapshotServerTime, AutoProxyCompleteness);
    }

    LatestReceivedTick = FMath::Max(LatestReceivedTick, Snapshot.TickNumber);
}

void AClientPredictionReplicationManager::AdjustServerTime() {
    const Chaos::FReal CurrentServerTime = CalculateTimeForServerTick(LatestReceivedTick);

    const Chaos::FReal ServerTimeDelta = CurrentServerTime - ServerTime;
    const Chaos::FReal DeltaAbs = FMath::Abs(ServerTimeDelta);

    if (DeltaAbs >= Settings->SimProxySnapTimeDifference) {
        ServerTime = CurrentServerTime;
        ServerTargetTimescale = 1.0;

        return;
    }

    if (DeltaAbs >= Settings->SimProxyAggressiveTimeDifference) {
        ServerTime = (CurrentServerTime + ServerTime) / 2.0;
    }

    ServerTargetTimescale = FMath::Sign(ServerTimeDelta) * Settings->SimProxyTimeDilation + 1.0;
}

Chaos::FReal AClientPredictionReplicationManager::CalculateTimeForServerTick(const int32 Tick) const {
    return static_cast<double>(Tick - ServerStartTick) * Settings->FixedDt + ServerStartTime;
}

float AClientPredictionReplicationManager::EstimateServerTick(const Chaos::FReal Time) const {
    const Chaos::FReal TimeFromStart = Time - ServerStartTime;
    return TimeFromStart / Settings->FixedDt + static_cast<float>(ServerStartTick);
}
