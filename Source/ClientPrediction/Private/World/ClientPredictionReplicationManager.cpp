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

	Manager->RegisterLocalReplicationManager(OwningController, this);
}

void AClientPredictionReplicationManager::PostTickAuthority() {
	const UPlayer* OwningPlayer = GetOwner()->GetNetOwningPlayer();
	check(OwningPlayer);

	// Go to state store, get all simulation outputs
	// Determine which outputs belong to this player using FClientPredictionModelId::MapToOwningPlayer
	// Serialize based on the relevance (auto proxies have always relevance and get all of the data)
	// Send data to client

	// Ask the input store for the recommended client time dilation and send that as well
}

void AClientPredictionReplicationManager::PostTickRemote() {

}
