#include "World/ClientPredictionReplicationManager.h"
#include "World/ClientPredictionWorldManager.h"

AClientPredictionReplicationManager::AClientPredictionReplicationManager() {
	PrimaryActorTick.bCanEverTick = false;
	bAlwaysRelevant = true;
	bReplicates = true;

	SetReplicateMovement(false);
}

bool AClientPredictionReplicationManager::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const {
	return RealViewer == GetOwner();
}

void AClientPredictionReplicationManager::PostNetInit() {
	Super::PostNetInit();

	APlayerController* OwningController = Cast<APlayerController>(GetOwner());
	if (OwningController == nullptr) { return; }

	ClientPrediction::FWorldManager* Manager = ClientPrediction::FWorldManager::ManagerForWorld(GetWorld());
	if (Manager == nullptr) { return; }

	Manager->RegisterReplicationManager(OwningController, this);
}
