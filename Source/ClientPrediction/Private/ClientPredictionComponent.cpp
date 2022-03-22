#include "ClientPredictionComponent.h"

#include "ClientPredictionPhysicsModel.h"
#include "PBDRigidsSolver.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

static constexpr Chaos::FReal kFixedDt = 0.0166666;

UClientPredictionComponent::UClientPredictionComponent() {
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();

	check(UpdatedComponent);
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
		Model->Tick(kFixedDt, UpdatedComponent, GetOwnerRole());
	}
	
	// Send states to the client(s)
	while (!QueuedClientSendStates.IsEmpty()) {
		FNetSerializationProxy Proxy;
		QueuedClientSendStates.Dequeue(Proxy);
		
		RecvServerState(Proxy);
	}

	// Send input packets to the authority
	while (!InputBufferSendQueue.IsEmpty()) {
		FNetSerializationProxy Proxy;
		InputBufferSendQueue.Dequeue(Proxy);
		
		RecvInputPacket(Proxy);
	}
	
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
