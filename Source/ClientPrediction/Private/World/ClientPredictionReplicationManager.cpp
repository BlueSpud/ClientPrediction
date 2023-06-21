#include "World/ClientPredictionReplicationManager.h"

#include "Net/UnrealNetwork.h"
#include "World/ClientPredictionWorldManager.h"

AClientPredictionReplicationManager::AClientPredictionReplicationManager() : Settings(GetDefault<UClientPredictionSettings>()) {
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

	ClientPrediction::FWorldManager* Manager = ClientPrediction::FWorldManager::ManagerForWorld(GetWorld());
	if (Manager == nullptr) { return; }

	APlayerController* OwningController = Cast<APlayerController>(GetOwner());
	if (OwningController == nullptr) { return; }

	Manager->RegisterLocalReplicationManager(OwningController, this);
}

void AClientPredictionReplicationManager::PostTickAuthority(int32 TickNumber) {
	if (StateManager == nullptr) { return; }

	const UPlayer* OwningPlayer = GetOwner()->GetNetOwningPlayer();
	check(OwningPlayer);

	ClientPrediction::FTickSnapshot TickSnapshot{};
	StateManager->GetProducedDataForTick(TickNumber, TickSnapshot);

	FScopeLock QueuedSnapshotLock(&QueuedSnapshotMutex);
	QueuedSnapshot = {};
	QueuedSnapshot.TickNumber = TickNumber;

	for (auto& Pair : TickSnapshot.StateData) {
		const UPlayer* ModelOwner = Pair.Key.MapToOwningPlayer();
		QueuedSnapshot.AutoProxyModels.Add({Pair.Key, MoveTemp(ModelOwner == OwningPlayer ? Pair.Value.FullData : Pair.Value.ShortData)});
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

void AClientPredictionReplicationManager::PostSceneTickGameThreadRemote() {
	const double Latency = GetNetConnection()->AvgLag;
	const double WorldTime = GetWorld()->GetRealTimeSeconds();
	const double WorldDt = WorldTime - LastWorldTime;

	const double EstimatedServerTime = WorldTime - (Latency / 2.0);
	const double TargetInterpolationTime = EstimatedServerTime - Settings->SimProxyDelay;

	if (InterpolationTime == -1) {
		InterpolationTime = TargetInterpolationTime;
		LastWorldTime = WorldTime;
		return;
	}

	const double PreliminaryNewInterpolationTime = InterpolationTime + WorldDt;
	const double TargetTimescale = FMath::Sign(TargetInterpolationTime - PreliminaryNewInterpolationTime) * Settings->SimProxyTimeDilation + 1.0;
	InterpolationTimescale = FMath::Lerp(InterpolationTimescale, TargetTimescale, Settings->SimProxyTimeDilationAlpha);

	InterpolationTime += WorldDt * InterpolationTimescale;
	LastWorldTime = WorldTime;

	UE_LOG(LogTemp, Warning, TEXT("%f %f"), InterpolationTimescale, InterpolationTime);
}

void AClientPredictionReplicationManager::SnapshotReceivedRemote() {
	if (StateManager == nullptr) { return; }

	for (const auto& SimProxy : RemoteSnapshot.SimProxyModels) {
		StateManager->PushStateToConsumer(RemoteSnapshot.TickNumber, SimProxy.ModelId, SimProxy.Data);
	}
}
