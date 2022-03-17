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

	PhysicsSimulation = new ImmediatePhysics::FSimulation();
	PhysicsSimulation->SetSolverIterations(kFixedDt, 5, 5, 5, 5, 5, 5);
}

UClientPredictionComponent::~UClientPredictionComponent() {
	delete PhysicsSimulation;
};

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();

	check(UpdatedComponent);
	Model->Initialize(UpdatedComponent, GetOwnerRole());

	UpdatedComponentPhysicsHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::DynamicActor, &UpdatedComponent->BodyInstance, UpdatedComponent->GetComponentTransform());
	check(UpdatedComponentPhysicsHandle);
	UpdatedComponentPhysicsHandle->SetEnabled(true);
	PhysicsSimulation->SetNumActiveBodies(1, {0});

	UWorld* UnsafeWorld = GetWorld();
	FPhysScene* PhysScene = UnsafeWorld->GetPhysicsScene();
	
	if ((UnsafeWorld != nullptr) && (PhysScene != nullptr))
	{
		TArray<FOverlapResult> Overlaps;
		AActor* Owner = Cast<AActor>(GetOwner());
		UnsafeWorld->OverlapMultiByChannel(Overlaps, Owner->GetActorLocation(), FQuat::Identity, ECollisionChannel::ECC_Visibility, FCollisionShape::MakeSphere(10000.0), FCollisionQueryParams::DefaultQueryParam, FCollisionResponseParams(ECR_Overlap));

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent())
			{
				const bool bIsSelf = (GetOwner() == OverlapComp->GetOwner());
				if (!bIsSelf)
				{
					// Create a kinematic actor. Not using Static as world-static objects may move in the simulation's frame of reference
					ImmediatePhysics::FActorHandle* ActorHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::KinematicActor, &OverlapComp->BodyInstance, OverlapComp->GetComponentTransform());
					PhysicsSimulation->AddToCollidingPairs(ActorHandle);
				}
			}
		}
	}
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
}

void UClientPredictionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AccumulatedTime += DeltaTime;
	while (AccumulatedTime >= kFixedDt) {
		AccumulatedTime = FMath::Clamp(AccumulatedTime - kFixedDt, 0.0, AccumulatedTime);
		
		PrePhysicsAdvance(kFixedDt);
		PhysicsSimulation->Simulate(kFixedDt, 1.0, 1, FVector(0.0, 0.0, -980.0));
		OnPhysicsAdvanced(kFixedDt);
	}
	
	Model->GameThreadTick(DeltaTime, UpdatedComponent, UpdatedComponentPhysicsHandle, GetOwnerRole());
	
	// Send states to the client
	while (!QueuedClientSendStates.IsEmpty()) {
		FNetSerializationProxy Proxy;
		QueuedClientSendStates.Dequeue(Proxy);
		
		RecvServerState(Proxy);
	}

	// Send 
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

void UClientPredictionComponent::PrePhysicsAdvance(Chaos::FReal Dt) {
	check(Model);

	Model->PreTick(kFixedDt, bIsForceSimulating, UpdatedComponent, UpdatedComponentPhysicsHandle, GetOwnerRole());
}

void UClientPredictionComponent::OnPhysicsAdvanced(Chaos::FReal Dt) {
	check(Model);
	
	Model->PostTick(kFixedDt, bIsForceSimulating, UpdatedComponent, UpdatedComponentPhysicsHandle, GetOwnerRole());
}

void UClientPredictionComponent::ForceSimulate(uint32 Frames) {
	bIsForceSimulating = true;
	for (uint32 i = 0; i < Frames; i++) {
		PrePhysicsAdvance(kFixedDt);
		PhysicsSimulation->Simulate(kFixedDt, 1.0, 1, FVector(0.0, 0.0, -980.0));
		OnPhysicsAdvanced(kFixedDt);
	}
	bIsForceSimulating = false;
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveInputPackets(Proxy);
}
void UClientPredictionComponent::RecvServerState_Implementation(FNetSerializationProxy Proxy) {
	check(Model);
	Model->ReceiveAuthorityState(Proxy);
}
