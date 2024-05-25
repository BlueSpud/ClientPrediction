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

    void FSimProxyWorldManager::ReceivedSimProxyStates(const FNetTickInfo& TickInfo, const int32 LatestReceivedServerTick) {
        if (LatestReceivedServerTick == INDEX_NONE || TickInfo.bIsResim) { return; }

        const int32 NewLocalOffset = LatestReceivedServerTick - TickInfo.LocalTick - kSimProxyBufferTicks;
        if (LocalToServerOffset == INDEX_NONE || FMath::Abs(LocalToServerOffset - NewLocalOffset) >= kSimProxyBufferCorrectionThreshold) {
            UE_LOG(LogTemp, Log, TEXT("Updating sim proxy offset to %d"), NewLocalOffset);
            LocalToServerOffset = NewLocalOffset;

            // This offset can be added to a server tick on the authority to get the tick for sim proxies that is being displayed
            const int32 AuthorityServerOffset = LatestReceivedServerTick - kSimProxyBufferTicks - TickInfo.ServerTick;
            RemoteSimProxyOffset = {TickInfo.ServerTick, AuthorityServerOffset};

            RemoteSimProxyOffsetChangedDelegate.Broadcast(RemoteSimProxyOffset);
        }
    }
}
