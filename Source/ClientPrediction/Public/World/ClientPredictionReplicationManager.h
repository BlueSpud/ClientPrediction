#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Data/ClientPredictionModelId.h"
#include "Data/ClientPredictionStateManager.h"

#include "ClientPredictionReplicationManager.generated.h"

namespace ClientPrediction {
    struct FStateManager;
}

// Data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FModelSnapshot {
    GENERATED_BODY()

    FClientPredictionModelId ModelId;
    TArray<uint8> Data;
};

USTRUCT()
struct FTickSnapshot {
    GENERATED_BODY()

    int32 TickNumber = -1;
    TArray<FModelSnapshot> SimProxyModels;
    TArray<FModelSnapshot> AutoProxyModels;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FTickSnapshot* Other, uint32 PortFlags) const;
};

template <>
struct TStructOpsTypeTraits<FTickSnapshot> : public TStructOpsTypeTraitsBase2<FTickSnapshot> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};

// Replication Manager
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UCLASS()
class CLIENTPREDICTION_API AClientPredictionReplicationManager : public AActor {
    GENERATED_BODY()

public:
    AClientPredictionReplicationManager();
    void SetStateManager(struct ClientPrediction::FStateManager* NewStateManager) { StateManager = NewStateManager; }

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PostNetInit() override;

public:
    void PostTickAuthority(int32 TickNumber);

    void PostSceneTickGameThreadAuthority();
    void PostSceneTickGameThreadRemote();

private:
    UFUNCTION()
    void SnapshotReceivedRemote();

    int32 ServerStartTick = INDEX_NONE;
    Chaos::FReal ServerStartTime = -1.0;
    Chaos::FReal ServerTimescale = 1.0;
    Chaos::FReal ServerTime = -1.0;

    Chaos::FReal LastWorldTime = -1.0;

    UPROPERTY()
    const UClientPredictionSettings* Settings = nullptr;

    struct ClientPrediction::FStateManager* StateManager = nullptr;

    UPROPERTY(Replicated, Transient, ReplicatedUsing=SnapshotReceivedRemote)
    FTickSnapshot RemoteSnapshot{};

    FCriticalSection QueuedSnapshotMutex;
    FTickSnapshot QueuedSnapshot{};

};
