#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    struct CLIENTPREDICTION_API FSimProxyWorldManager {
    private:
        static TMap<class UWorld*, FSimProxyWorldManager*> Managers;

    public:
        static FSimProxyWorldManager* InitializeWorld(class UWorld* World);
        static FSimProxyWorldManager* ManagerForWorld(const class UWorld* World);
        static void CleanupWorld(const class UWorld* World);

        virtual ~FSimProxyWorldManager() = default;

    private:
        explicit FSimProxyWorldManager(class UWorld* World);

    public:
        void RecievedSimProxyStates(const int32 LatestReceivedServerTick);
        int32 GetOffsetFromServer() const { return OffsetFromServer; }

    private:
        UWorld* World = nullptr;
        int32 OffsetFromServer = INDEX_NONE;
    };
}
