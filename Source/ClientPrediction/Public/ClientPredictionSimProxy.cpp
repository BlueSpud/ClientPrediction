#include "ClientPredictionSimProxy.h"

#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

static int32 kSimProxyBufferTicks = 4;
static int32 kSimProxyBufferCorrectionThreshold = 6;

namespace ClientPrediction {
    TMap<UWorld*, FSimProxyWorldManager*> FSimProxyWorldManager::Managers;

    // Initialization

    FSimProxyWorldManager* FSimProxyWorldManager::InitializeWorld(UWorld* World) {
        check(!Managers.Contains(World));

        FSimProxyWorldManager* Manager = new FSimProxyWorldManager(World);
        Managers.Add(World, Manager);

        return Manager;
    }

    FSimProxyWorldManager* FSimProxyWorldManager::ManagerForWorld(const UWorld* World) {
        if (!Managers.Contains(World)) { return nullptr; }
        return Managers[World];
    }

    void FSimProxyWorldManager::CleanupWorld(const UWorld* World) {
        if (!Managers.Contains(World)) { return; }

        const FSimProxyWorldManager* WorldManager = Managers[World];
        Managers.Remove(World);

        delete WorldManager;
    }

    FSimProxyWorldManager::FSimProxyWorldManager(UWorld* World) : World(World) {}

    void FSimProxyWorldManager::ReceivedSimProxyStates(const int32 LatestReceivedServerTick) {
        if (LatestReceivedServerTick == INDEX_NONE) { return; }
        check(World != nullptr);

        FPhysScene* PhysScene = World->GetPhysicsScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = PhysScene->GetSolver();
        if (PhysSolver == nullptr) { return; }

        const int32 LocalTick = PhysSolver->GetCurrentFrame();
        const int32 NewOffset = LocalTick - LatestReceivedServerTick + kSimProxyBufferTicks;

        if (OffsetFromServer == INDEX_NONE || FMath::Abs(OffsetFromServer - NewOffset) >= kSimProxyBufferCorrectionThreshold) {
            UE_LOG(LogTemp, Log, TEXT("Updating sim proxy offset to %d"), NewOffset);

            OffsetFromServer = NewOffset;
        }
    }
}
