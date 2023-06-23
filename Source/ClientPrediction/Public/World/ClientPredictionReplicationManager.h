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
    virtual void PostNetInit() override;

    void SetStateManager(struct ClientPrediction::FStateManager* NewStateManager) { StateManager = NewStateManager; }

public:
    void PostTickAuthority(int32 TickNumber);

    void PostSceneTickGameThreadAuthority();
    void PostSceneTickGameThreadRemote();

private:
    UFUNCTION(Client, Unreliable)
    void SnapshotReceivedRemote(const FTickSnapshot& Snapshot);

    UFUNCTION(Client, Reliable)
    void SnapshotReceivedReliable(const FTickSnapshot& Snapshot);

    void ProcessSnapshot(const FTickSnapshot& Snapshot, bool bIsReliable);

    int32 ServerStartTick = INDEX_NONE;
    Chaos::FReal ServerStartTime = -1.0;
    Chaos::FReal ServerTimescale = 1.0;
    Chaos::FReal ServerTime = -1.0;

    Chaos::FReal LastWorldTime = -1.0;

    UPROPERTY()
    const UClientPredictionSettings* Settings = nullptr;

    struct ClientPrediction::FStateManager* StateManager = nullptr;

    FCriticalSection QueuedSnapshotMutex;
    TQueue<FTickSnapshot> QueuedSnapshots;
    TQueue<FTickSnapshot> ReliableSnapshotQueue;
};
