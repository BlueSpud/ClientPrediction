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
struct FReplicatedTickSnapshot {
    GENERATED_BODY()

    int32 TickNumber = -1;
    TArray<FModelSnapshot> SimProxyModels;
    TArray<FModelSnapshot> AutoProxyModels;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FReplicatedTickSnapshot* Other, uint32 PortFlags) const;
};

template <>
struct TStructOpsTypeTraits<FReplicatedTickSnapshot> : public TStructOpsTypeTraitsBase2<FReplicatedTickSnapshot> {
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
    void SnapshotReceivedRemote(const FReplicatedTickSnapshot& Snapshot);

    UFUNCTION(Client, Reliable)
    void SnapshotReceivedReliable(const FReplicatedTickSnapshot& Snapshot);

    void ProcessSnapshot(const TTuple<FReplicatedTickSnapshot, ClientPrediction::EDataCompleteness>& Tuple);
    void AdjustServerTime();

    Chaos::FReal CalculateTimeForServerTick(const int32 Tick) const;
    float EstimateServerTick(const Chaos::FReal Time) const;

    int32 ServerStartTick = INDEX_NONE;
    Chaos::FReal ServerStartTime = -1.0;
    Chaos::FReal ServerTargetTimescale = 1.0;
    Chaos::FReal ServerTimescale = 1.0;
    Chaos::FReal ServerTime = -1.0;

    Chaos::FReal LastWorldTime = -1.0;

    UPROPERTY()
    const UClientPredictionSettings* Settings = nullptr;

    struct ClientPrediction::FStateManager* StateManager = nullptr;

    FCriticalSection SendQueueMutex;
    TQueue<FReplicatedTickSnapshot> SnapshotSendQueue;
    TQueue<FReplicatedTickSnapshot> ReliableSnapshotSendQueue;

    FCriticalSection ReceiveQueueMutex;
    TQueue<TTuple<FReplicatedTickSnapshot, ClientPrediction::EDataCompleteness>> SnapshotReceiveQueue;
    int32 LatestReceivedTick = INDEX_NONE;
};
