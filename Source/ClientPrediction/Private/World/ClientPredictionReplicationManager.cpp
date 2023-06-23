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
    bAlwaysRelevant = true;
    bReplicates = true;

    SetReplicateMovement(false);
}

void AClientPredictionReplicationManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AClientPredictionReplicationManager, RemoteSnapshot);
}

bool AClientPredictionReplicationManager::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const {
    return RealViewer == GetOwner();
}

void AClientPredictionReplicationManager::PostNetInit() {
    Super::PostNetInit();

    ClientPrediction::FWorldManager* Manager = ClientPrediction::FWorldManager::ManagerForWorld(GetWorld());
    if (Manager == nullptr) { return; }

    APlayerController* OwningController = Cast<APlayerController>(GetOwner());
    if (OwningController == nullptr) { return; }

    Manager->RegisterLocalReplicationManager(OwningController, this);
    LastWorldTime = GetWorld()->GetRealTimeSeconds();
}

void AClientPredictionReplicationManager::PostTickAuthority(int32 TickNumber) {
    if (StateManager == nullptr) { return; }

    const UPlayer* OwningPlayer = GetOwner()->GetNetOwningPlayer();
    check(OwningPlayer);

    ClientPrediction::FTickSnapshot TickSnapshot{};
    StateManager->GetProducedDataForTick(TickNumber, TickSnapshot);

    FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
    QueuedSnapshot = {};
    QueuedSnapshot.TickNumber = TickNumber;

    for (auto& Pair : TickSnapshot.StateData) {
        const UPlayer* ModelOwner = Pair.Key.MapToOwningPlayer();

        if (ModelOwner == OwningPlayer) {
            QueuedSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.FullData)});
        }
        else {
            QueuedSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.ShortData)});
        }
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadAuthority() {
    FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
    if (QueuedSnapshot.TickNumber != -1) {
        RemoteSnapshot = QueuedSnapshot;
        QueuedSnapshot = {};
    }
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadRemote() {
    const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();
    const Chaos::FReal WorldDt = WorldTime - LastWorldTime;

    ServerTime += WorldDt * ServerTimescale;
    LastWorldTime = WorldTime;

    StateManager->SetInterpolationTime(ServerTime - Settings->SimProxyDelay);
}

void AClientPredictionReplicationManager::SnapshotReceivedRemote() {
    if (StateManager == nullptr) { return; }

    if (ServerStartTick == INDEX_NONE) {
        const Chaos::FReal WorldTime = GetWorld()->GetRealTimeSeconds();

        ServerStartTick = RemoteSnapshot.TickNumber;
        ServerStartTime = WorldTime;

        ServerTime = WorldTime;
    }

    // Slow down the server time if it is too far ahead and speed it up if it is too far behind
    const Chaos::FReal SnapshotServerTime = static_cast<double>(RemoteSnapshot.TickNumber - ServerStartTick) * Settings->FixedDt + ServerStartTime;
    const Chaos::FReal TargetTimescale = FMath::Sign(SnapshotServerTime - ServerTime) * Settings->SimProxyTimeDilation + 1.0;

    ServerTimescale = FMath::Lerp(ServerTimescale, TargetTimescale, Settings->SimProxyTimeDilationAlpha);

    UE_LOG(LogTemp, Warning, TEXT("%s"), *GetOwner()->GetNetConnection()->GetName());

    for (const auto& AutoProxy : RemoteSnapshot.AutoProxyModels) {
        StateManager->PushStateToConsumer(RemoteSnapshot.TickNumber, AutoProxy.ModelId, AutoProxy.Data, SnapshotServerTime, ClientPrediction::kAutoProxy);
    }

    for (const auto& SimProxy : RemoteSnapshot.SimProxyModels) {
        StateManager->PushStateToConsumer(RemoteSnapshot.TickNumber, SimProxy.ModelId, SimProxy.Data, SnapshotServerTime, ClientPrediction::kRelevant);
    }
}
