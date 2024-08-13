#include "ClientPredictionSimProxy.h"

#include "Net/UnrealNetwork.h"

#include "ClientPrediction.h"
#include "ClientPredictionCVars.h"
#include "ClientPredictionUtils.h"

TMap<UWorld*, AClientPredictionSimProxyManager*> AClientPredictionSimProxyManager::Managers;

// Initialization

void AClientPredictionSimProxyManager::InitializeWorld(UWorld* World) {
    if (!World->IsGameWorld()) { return; }
    check(!Managers.Contains(World));

    FActorSpawnParameters SpawnParameters{};
    SpawnParameters.Name = FName(TEXT("ClientPredictionSimProxyManager"));
    SpawnParameters.ObjectFlags |= EObjectFlags::RF_Transient;

    AClientPredictionSimProxyManager* Manager = World->SpawnActor<AClientPredictionSimProxyManager>(SpawnParameters);
    check(Manager);

    Managers.Add(World, Manager);
}

AClientPredictionSimProxyManager* AClientPredictionSimProxyManager::ManagerForWorld(const UWorld* World) {
    if (!Managers.Contains(World)) { return nullptr; }
    return Managers[World];
}

void AClientPredictionSimProxyManager::CleanupWorld(const UWorld* World) {
    if (!Managers.Contains(World)) { return; }
    Managers.Remove(World);
}

AClientPredictionSimProxyManager::AClientPredictionSimProxyManager() {
    bReplicates = true;
    bAlwaysRelevant = true;

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    PrimaryActorTick.TickInterval = ClientPrediction::ClientPredictionSimProxyTickInterval;
}

void AClientPredictionSimProxyManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AClientPredictionSimProxyManager, LatestServerTick);
}

void AClientPredictionSimProxyManager::PostInitProperties() {
    Super::PostInitProperties();
    SetReplicateMovement(false);
}

void AClientPredictionSimProxyManager::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);

    if (GetLocalRole() != ROLE_Authority) {
        return;
    }

    Chaos::FPhysicsSolver* PhysSolver = ClientPrediction::FUtils::GetPhysSolver(GetWorld());
    if (PhysSolver == nullptr) { return; }

    LatestServerTick = PhysSolver->GetCurrentFrame();
}

int32 AClientPredictionSimProxyManager::GetLocalToServerOffset() const {
    FScopeLock OffsetsLock(&OffsetsMutex);
    return LocalToServerOffset;
}

const TOptional<FRemoteSimProxyOffset>& AClientPredictionSimProxyManager::GetRemoteSimProxyOffset() const {
    FScopeLock OffsetsLock(&OffsetsMutex);
    return RemoteSimProxyOffset;
}

void AClientPredictionSimProxyManager::LatestServerTickChangedGT() {
    if (!HasActorBegunPlay()) { return; }

    const UWorld* World = GetWorld();
    if (World == nullptr) { return; }

    FPhysScene* PhysScene = ClientPrediction::FUtils::GetPhysScene(World);
    if (PhysScene == nullptr) { return; }

    PhysScene->EnqueueAsyncPhysicsCommand(0, this, [this, LatestServerTick = LatestServerTick]() {
        LatestServerTickChangedPT(LatestServerTick);
    });
}

void AClientPredictionSimProxyManager::LatestServerTickChangedPT(int32 TickToProcess) {
    FScopeLock OffsetsLock(&OffsetsMutex);

    const UWorld* World = GetWorld();
    if (World == nullptr) { return; }

    Chaos::FPhysicsSolver* PhysSolver = ClientPrediction::FUtils::GetPhysSolver(GetWorld());
    if (PhysSolver == nullptr) { return; }

    ClientPrediction::FTickInfo TickInfo{};
    ClientPrediction::FUtils::FillTickInfo(TickInfo, PhysSolver->GetCurrentFrame(), GetLocalRole(), World);

    if (TickToProcess == INDEX_NONE || TickInfo.bIsResim) {
        return;
    }

    // If the server is ahead of the client we don't update the offset, since that wouldn't be valid.
    if (TickToProcess > TickInfo.ServerTick) {
        return;
    }

    const int32 NewLocalOffset = TickToProcess - TickInfo.LocalTick - ClientPrediction::ClientPredictionSimProxyBufferTicks;
    if (LocalToServerOffset == INDEX_NONE || FMath::Abs(LocalToServerOffset - NewLocalOffset) >= ClientPrediction::ClientPredictionSimProxyCorrectionThreshold) {
        LocalToServerOffset = NewLocalOffset;

        UE_LOG(LogClientPrediction, Log, TEXT("Updating sim proxy offset to %d. "), NewLocalOffset);
    }

    // This offset can be added to a server tick on the authority to get the tick for sim proxies that is being displayed
    const int32 AuthorityServerOffset = TickInfo.LocalTick + LocalToServerOffset - TickInfo.ServerTick;
    if (!RemoteSimProxyOffset.IsSet() || RemoteSimProxyOffset.GetValue().ServerTickOffset != AuthorityServerOffset) {
        RemoteSimProxyOffset = {TickInfo.ServerTick, AuthorityServerOffset};

        UE_LOG(LogClientPrediction, Log, TEXT("Updating remote sim proxy offset %d"), AuthorityServerOffset);
        RemoteSimProxyOffsetChangedDelegate.Broadcast(RemoteSimProxyOffset);
    }
}
