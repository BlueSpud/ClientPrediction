#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionTick.h"
#include "ClientPredictionSimProxy.generated.h"

USTRUCT()
struct FRemoteSimProxyOffset {
    GENERATED_BODY()

    int32 ExpectedAppliedServerTick = INDEX_NONE;
    int32 ServerTickOffset = 0;

    inline bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

bool FRemoteSimProxyOffset::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    Ar << ExpectedAppliedServerTick;
    Ar << ServerTickOffset;

    return true;
}

template <>
struct TStructOpsTypeTraits<FRemoteSimProxyOffset> : public TStructOpsTypeTraitsBase2<FRemoteSimProxyOffset> {
    enum {
        WithNetSerializer = true
    };
};

namespace ClientPrediction {
    struct CLIENTPREDICTION_API FSimProxyWorldManager {
    private:
        static TMap<class UWorld*, FSimProxyWorldManager*> Managers;

    public:
        static void InitializeWorld(class UWorld* World);
        static FSimProxyWorldManager* ManagerForWorld(const class UWorld* World);
        static void CleanupWorld(const class UWorld* World);

        virtual ~FSimProxyWorldManager() = default;

    private:
        explicit FSimProxyWorldManager(class UWorld* World);

    public:
        void ReceivedSimProxyStates(const FNetTickInfo& TickInfo, const int32 LatestReceivedServerTick);
        int32 GetLocalToServerOffset() const { return LocalToServerOffset; }
        const TOptional<FRemoteSimProxyOffset>& GetRemoteSimProxyOffset() const { return RemoteSimProxyOffset; }

    private:
        UWorld* World = nullptr;

        /** This offset can be added to a local tick to get the server tick for sim proxies. */
        int32 LocalToServerOffset = INDEX_NONE;
        TOptional<FRemoteSimProxyOffset> RemoteSimProxyOffset{};

    public:
        DECLARE_MULTICAST_DELEGATE_OneParam(FRemoteSimProxyOffsetChangedDelegate, const TOptional<FRemoteSimProxyOffset>& Offset)
        FRemoteSimProxyOffsetChangedDelegate RemoteSimProxyOffsetChangedDelegate;
    };
}
