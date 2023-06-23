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

bool FTickSnapshot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    Ar << TickNumber;
    NetSerializeSnapshotArray(Ar, Map, SimProxyModels);
    NetSerializeSnapshotArray(Ar, Map, AutoProxyModels);

    bOutSuccess = true;
    return true;
}

bool FTickSnapshot::Identical(const FTickSnapshot* Other, uint32 PortFlags) const {
    return TickNumber == Other->TickNumber;
}

// Replication manager
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AClientPredictionReplicationManager::AClientPredictionReplicationManager() : Settings(GetDefault<UClientPredictionSettings>()) {
    PrimaryActorTick.bCanEverTick = false;
    bOnlyRelevantToOwner = true;
    bAlwaysRelevant = true;
    bReplicates = true;

    SetReplicateMovement(false);
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

    const UPlayer* OwningPlayer = GetOwner()->GetNetOwningPlayer();
    check(OwningPlayer);

    ClientPrediction::FTickSnapshot TickSnapshot{};
    StateManager->GetProducedDataForTick(TickNumber, TickSnapshot);

    FTickSnapshot NewSnapshot = {};
    NewSnapshot.TickNumber = TickNumber;

    FTickSnapshot ReliableSnapshot{};
    ReliableSnapshot.TickNumber = TickNumber;

    for (auto& Pair : TickSnapshot.StateData) {
        const UPlayer* ModelOwner = Pair.Key.MapToOwningPlayer();
        const bool bIsOwnedByCurrentPlayer = ModelOwner == OwningPlayer;

        if (Pair.Value.bShouldBeReliable) {
            // Reliable messages should always include the full state because the main reason is to have the events
            if (bIsOwnedByCurrentPlayer) {
                ReliableSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.FullData)});
            }
            else {
                ReliableSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.FullData)});
            }
        }
        else {
            if (bIsOwnedByCurrentPlayer) {
                NewSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.FullData)});
            }
            else {
                NewSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.ShortData)});
            }
        }
    }

    FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
    QueuedSnapshots.Enqueue(MoveTemp(NewSnapshot));

    // If there is data that needs to be reliably sent, it is done so over a reliable RPC
    if (!ReliableSnapshot.AutoProxyModels.IsEmpty() || !ReliableSnapshot.SimProxyModels.IsEmpty()) {
        ReliableSnapshotQueue.Enqueue(MoveTemp(ReliableSnapshot));
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadAuthority() {
    FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
    while (!QueuedSnapshots.IsEmpty()) {
        SnapshotReceivedRemote(*QueuedSnapshots.Peek());
        QueuedSnapshots.Pop();
    }

    while (!ReliableSnapshotQueue.IsEmpty()) {
        SnapshotReceivedReliable(*ReliableSnapshotQueue.Peek());
        ReliableSnapshotQueue.Pop();
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadRemote() {
    const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();
    const Chaos::FReal WorldDt = WorldTime - LastWorldTime;

    ServerTime += WorldDt * ServerTimescale;
    LastWorldTime = WorldTime;

    StateManager->SetInterpolationTime(ServerTime - Settings->SimProxyDelay);
}

void AClientPredictionReplicationManager::SnapshotReceivedRemote_Implementation(const FTickSnapshot& Snapshot) {
    ProcessSnapshot(Snapshot, false);
}

void AClientPredictionReplicationManager::SnapshotReceivedReliable_Implementation(const FTickSnapshot& Snapshot) {
    ProcessSnapshot(Snapshot, true);
}

void AClientPredictionReplicationManager::ProcessSnapshot(const FTickSnapshot& Snapshot, bool bIsReliable) {
    if (StateManager == nullptr) { return; }

    if (ServerStartTick == INDEX_NONE) {
        const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();

        ServerStartTick = Snapshot.TickNumber;
        ServerStartTime = WorldTime;

        ServerTime = WorldTime;
    }

    // Slow down the server time if it is too far ahead and speed it up if it is too far behind
    const Chaos::FReal SnapshotServerTime = static_cast<double>(Snapshot.TickNumber - ServerStartTick) * Settings->FixedDt + ServerStartTime;

    if (!bIsReliable) {
        const Chaos::FReal ServerTimeDelta = SnapshotServerTime - ServerTime;
        const Chaos::FReal DeltaAbs = FMath::Abs(ServerTimeDelta);

        if (DeltaAbs >= Settings->SimProxySnapTimeDifference) {
            ServerTime = SnapshotServerTime;
            ServerTimescale = 1.0;
        }
        else if (DeltaAbs >= Settings->SimProxyAggressiveTimeDifference) {
            ServerTime = (SnapshotServerTime + ServerTime) / 2.0;
        }

        const Chaos::FReal TargetTimescale = FMath::Sign(ServerTimeDelta) * Settings->SimProxyTimeDilation + 1.0;
        ServerTimescale = FMath::Lerp(ServerTimescale, TargetTimescale, Settings->SimProxyTimeDilationAlpha);
    }

    for (const auto& AutoProxy : Snapshot.AutoProxyModels) {
        StateManager->PushStateToConsumer(Snapshot.TickNumber, AutoProxy.ModelId, AutoProxy.Data, SnapshotServerTime, ClientPrediction::kFull);
    }

    const ClientPrediction::EDataCompleteness AutoProxyCompleteness = bIsReliable ? ClientPrediction::kFull : ClientPrediction::kStandard;
    for (const auto& SimProxy : Snapshot.SimProxyModels) {
        StateManager->PushStateToConsumer(Snapshot.TickNumber, SimProxy.ModelId, SimProxy.Data, SnapshotServerTime, AutoProxyCompleteness);
    }
}
