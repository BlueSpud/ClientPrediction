#include "ClientPredictionComponent.h"

#include "Net/UnrealNetwork.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

#include "Declares.h"

UClientPredictionComponent::UClientPredictionComponent() {
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
}

void UClientPredictionComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UClientPredictionComponent, AutoProxyRep, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION(UClientPredictionComponent, SimProxyRep, COND_SimulatedOnly);
}

void UClientPredictionComponent::InitializeComponent() {
	Super::InitializeComponent();

	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	check(UpdatedComponent);

	CheckOwnerRoleChanged();
}

void UClientPredictionComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) {
	Super::PreReplication(ChangedPropertyTracker);
	CheckOwnerRoleChanged();
}

void UClientPredictionComponent::PreNetReceive() {
	Super::PreNetReceive();
	CheckOwnerRoleChanged();
}

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();

	CheckOwnerRoleChanged();
	Model->Initialize(UpdatedComponent);
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
}

void UClientPredictionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	Model->Update(DeltaTime, UpdatedComponent);
}

void UClientPredictionComponent::RecvReliableAuthorityState_Implementation(FNetSerializationProxy Proxy) {
	Model->ReceiveReliableAuthorityState(Proxy);
}

float UClientPredictionComponent::GetRtt() const {
	const AActor* Owner = GetOwner();
	if (!Owner) { return 0.0f; }

	const UNetConnection* NetConnection = Owner->GetNetConnection();
	if (NetConnection == nullptr) { return 0.0f; }

	return NetConnection->AvgLag;
}

void UClientPredictionComponent::CheckOwnerRoleChanged() {
	const AActor* OwnerActor = GetOwner();
	const ENetRole CurrentRole = OwnerActor->GetLocalRole();
	const bool bAuthorityTakesInput = OwnerActor->GetNetConnection() == nullptr;

	if (CachedRole == CurrentRole || bCachedAuthorityTakesInput == bAuthorityTakesInput) { return; }

	CachedRole = CurrentRole;
	bCachedAuthorityTakesInput = bAuthorityTakesInput;

	Model->SetNetRole(CachedRole, bAuthorityTakesInput, AutoProxyRep, SimProxyRep);
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveInputPackets(Proxy);
}
