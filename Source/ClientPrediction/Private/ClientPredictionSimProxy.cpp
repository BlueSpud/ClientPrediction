#include "ClientPredictionSimProxy.h"

#include "ClientPrediction.h"
#include "ClientPredictionCVars.h"

namespace ClientPrediction {
    TMap<UWorld*, FSimProxyWorldManager*> FSimProxyWorldManager::Managers;

    // Initialization

    void FSimProxyWorldManager::InitializeWorld(UWorld* World) {
        if (!World->IsGameWorld()) { return; }
        check(!Managers.Contains(World));

        FSimProxyWorldManager* Manager = new FSimProxyWorldManager(World);
        Managers.Add(World, Manager);
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

        // If the server is ahead of the client we don't update the offset, since that wouldn't be valid.
        if (LatestReceivedServerTick > TickInfo.ServerTick) {
            return;
        }

        const int32 NewLocalOffset = LatestReceivedServerTick - TickInfo.LocalTick - ClientPredictionSimProxyBufferTicks;
        if (LocalToServerOffset == INDEX_NONE || FMath::Abs(LocalToServerOffset - NewLocalOffset) >= ClientPredictionSimProxyCorrectionThreshold) {
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
}
