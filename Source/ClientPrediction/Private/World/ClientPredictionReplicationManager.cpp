#include "World/ClientPredictionReplicationManager.h"

#include "Net/UnrealNetwork.h"
#include "World/ClientPredictionWorldManager.h"

AClientPredictionReplicationManager::AClientPredictionReplicationManager() {
	PrimaryActorTick.bCanEverTick = false;
	bAlwaysRelevant = true;
	bReplicates = true;

	SetReplicateMovement(false);
}

void AClientPredictionReplicationManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AClientPredictionReplicationManager, RemoteSnapshot);
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

void AClientPredictionReplicationManager::PostTickAuthority(int32 TickNumber, const ClientPrediction::FStateManager& StateManager) {
	const UPlayer* OwningPlayer = GetOwner()->GetNetOwningPlayer();
	check(OwningPlayer);

	ClientPrediction::FTickSnapshot TickSnapshot{};
	StateManager.GetProducedDataForTick(TickNumber, TickSnapshot);

	FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
	QueuedSnapshot = {};
	QueuedSnapshot.TickNumber = TickNumber;

	for (auto& Pair : TickSnapshot.StateData) {
		const UPlayer* ModelOwner = Pair.Key.MapToOwningPlayer();

		if (ModelOwner == OwningPlayer) {
			QueuedSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.FullData)});
		}
		else {
			QueuedSnapshot.SimProxyModels.Add({Pair.Key, MoveTemp(Pair.Value.ShortData)});
		}
	}
}

void AClientPredictionReplicationManager::PostTickRemote() {
	// Ask the input store for all of the pending inputs
	// All of the models in the input store should be for the current client, since only models that are auto proxies will be input producers
	// Build them into frames
	// Send them to the authority
}

void AClientPredictionReplicationManager::PostSceneTickGameThreadAuthority() {
	FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
	if (QueuedSnapshot.TickNumber != -1) {
		RemoteSnapshot = QueuedSnapshot;
		QueuedSnapshot = {};
	}
}

void AClientPredictionReplicationManager::SnapshotReceivedRemote() {

	UE_LOG(LogTemp, Error, TEXT("FRAME %d"), RemoteSnapshot.TickNumber);

	for (const auto& AutoProxy : RemoteSnapshot.AutoProxyModels) {
		UE_LOG(LogTemp, Warning, TEXT("AUTO PROXY %d"), AutoProxy.Data.Num());
	}

	for (const auto& SimProxy : RemoteSnapshot.SimProxyModels) {
		UE_LOG(LogTemp, Warning, TEXT("SIM PROXY %d"), SimProxy.Data.Num());
	}
}
