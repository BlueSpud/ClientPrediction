#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Data/ClientPredictionModelId.h"
#include "Data/ClientPredictionStateManager.h"

#include "ClientPredictionReplicationManager.generated.h"

namespace ClientPrediction {
	struct FStateManager;
}

USTRUCT()
struct FModelSnapshot {
	GENERATED_BODY()

	UPROPERTY()
	FClientPredictionModelId ModelId;

	UPROPERTY()
	TArray<uint8> Data;
};

USTRUCT()
struct FTickSnapshot {
	GENERATED_BODY()

	UPROPERTY()
	int32 TickNumber = -1;

	UPROPERTY()
	TArray<FModelSnapshot> SimProxyModels;

	UPROPERTY()
	TArray<FModelSnapshot> AutoProxyModels;
};

UCLASS()
class CLIENTPREDICTION_API AClientPredictionReplicationManager : public AActor {
	GENERATED_BODY()

public:
	AClientPredictionReplicationManager();
	void SetStateManager(struct ClientPrediction::FStateManager* NewStateManager) { StateManager = NewStateManager; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	virtual void PostNetInit() override;

public:
	void PostTickAuthority(int32 TickNumber);
	void PostTickRemote();
	void PostSceneTickGameThreadAuthority();

private:
	UFUNCTION()
	void SnapshotReceivedRemote();

	struct ClientPrediction::FStateManager* StateManager = nullptr;

	UPROPERTY(Replicated, Transient, ReplicatedUsing=SnapshotReceivedRemote)
	FTickSnapshot RemoteSnapshot{};

	FCriticalSection QueuedSnapshotMutex;
	FTickSnapshot QueuedSnapshot{};
};
