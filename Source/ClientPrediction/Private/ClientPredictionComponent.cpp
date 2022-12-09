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
	TestPhysicsModel.Initialize(UpdatedComponent, this);

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

void UClientPredictionComponent::CheckOwnerRoleChanged() {
	const AActor* OwnerActor = GetOwner();
	const ENetRole CurrentRole = OwnerActor->GetLocalRole();
	const bool bAuthorityTakesInput = OwnerActor->GetNetConnection() == nullptr;

	if (CachedRole == CurrentRole && bCachedAuthorityTakesInput == static_cast<uint8>(bAuthorityTakesInput)) { return; }

	CachedRole = CurrentRole;
	bCachedAuthorityTakesInput = bAuthorityTakesInput;

	TestPhysicsModel.SetNetRole(CurrentRole, bAuthorityTakesInput, AutoProxyRep, SimProxyRep);
}

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();
	CheckOwnerRoleChanged();
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
	TestPhysicsModel.Cleanup();
}

void UClientPredictionComponent::RecvReliableAuthorityState_Implementation(FNetSerializationProxy Proxy) {

}

void UClientPredictionComponent::EmitInputPackets(FNetSerializationProxy& Proxy) {
	RecvInputPacket(Proxy);
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	TestPhysicsModel.ReceiveInputPackets(Proxy);
}
