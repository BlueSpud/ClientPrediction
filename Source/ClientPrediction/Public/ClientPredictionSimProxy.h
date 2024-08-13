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

UCLASS()
class CLIENTPREDICTION_API AClientPredictionSimProxyManager : public AActor {
    GENERATED_BODY()

    static TMap<class UWorld*, AClientPredictionSimProxyManager*> Managers;

public:
    static void InitializeWorld(class UWorld* World);
    static AClientPredictionSimProxyManager* ManagerForWorld(const class UWorld* World);
    static void CleanupWorld(const class UWorld* World);

    AClientPredictionSimProxyManager();
    virtual void PostInitProperties() override;

    virtual void Tick(float DeltaSeconds) override;

    int32 GetLocalToServerOffset() const;
    const TOptional<FRemoteSimProxyOffset>& GetRemoteSimProxyOffset() const;

private:
    UFUNCTION()
    void LatestServerTickChangedGT();
    void LatestServerTickChangedPT(const int32 TickToProcess);

    UPROPERTY(ReplicatedUsing=LatestServerTickChangedGT)
    int32 LatestServerTick = INDEX_NONE;

    mutable FCriticalSection OffsetsMutex{};

    /** This offset can be added to a local tick to get the server tick for sim proxies. */
    int32 LocalToServerOffset = INDEX_NONE;
    TOptional<FRemoteSimProxyOffset> RemoteSimProxyOffset{};

public:
    DECLARE_MULTICAST_DELEGATE_OneParam(FRemoteSimProxyOffsetChangedDelegate, const TOptional<FRemoteSimProxyOffset>& Offset)
    FRemoteSimProxyOffsetChangedDelegate RemoteSimProxyOffsetChangedDelegate;
};
