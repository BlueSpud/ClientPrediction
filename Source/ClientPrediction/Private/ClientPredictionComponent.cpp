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
	CheckOwnerRoleChanged();

	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	check(UpdatedComponent);
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
	Model->Initialize(UpdatedComponent, GetOwnerRole());
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
}

void UClientPredictionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AccumulatedTime += DeltaTime;
	while (AccumulatedTime >= kFixedDt) {
		AccumulatedTime = FMath::Clamp(AccumulatedTime - kFixedDt, 0.0, AccumulatedTime);
		Model->Tick(kFixedDt, UpdatedComponent);
	}

	Model->Finalize(AccumulatedTime / kFixedDt, UpdatedComponent);
}

void UClientPredictionComponent::RecvReliableAuthorityState_Implementation(FNetSerializationProxy Proxy) {
	Model->ReceiveReliableAuthorityState(Proxy);
}

void UClientPredictionComponent::CheckOwnerRoleChanged() {
	if (CachedRole == GetOwnerRole()) { return; }
	CachedRole = GetOwnerRole();

	// TODO fix the take input, it broke with the check owner role changed
	Model->SetNetRole(CachedRole, false, AutoProxyRep, SimProxyRep);
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveInputPackets(Proxy);
}
