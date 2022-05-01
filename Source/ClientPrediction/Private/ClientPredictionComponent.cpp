#include "ClientPredictionComponent.h"

#include "PBDRigidsSolver.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

#include "Declares.h"
#include "Animation/AnimSubsystem_NodeRelevancy.h"

UClientPredictionComponent::UClientPredictionComponent() {
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
}

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();
	check(UpdatedComponent);
	
	bool bSimulationOwnedByAuthority = false;
	APawn* Pawn = Cast<APawn>(UpdatedComponent->GetOwner());
	if (Pawn != nullptr && Pawn->GetLocalRole() == ENetRole::ROLE_Authority) {
		bSimulationOwnedByAuthority = Pawn->IsLocallyControlled();
	}
	
	Model->PreInitialize(GetOwnerRole(), bSimulationOwnedByAuthority);
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

void UClientPredictionComponent::OnRegister() {
	Super::OnRegister();

	// TODO make this more sophisticated
	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveInputPackets(Proxy);
}
void UClientPredictionComponent::RecvServerState_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveAuthorityState(Proxy);
}
