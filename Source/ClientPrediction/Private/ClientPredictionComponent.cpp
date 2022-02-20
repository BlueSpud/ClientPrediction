#include "ClientPredictionComponent.h"

#include "ClientPredictionPhysicsModel.h"
#include "PBDRigidsSolver.h"

UClientPredictionComponent::UClientPredictionComponent() : Model() {
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

UClientPredictionComponent::~UClientPredictionComponent() = default;

void UClientPredictionComponent::BeginPlay() {
	Super::BeginPlay();
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	OnPhysicsAdvancedDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateUObject(this, &UClientPredictionComponent::OnPhysicsAdvanced));
	PrePhysicsAdvancedDelegate = Solver->AddPreAdvanceCallback(FSolverPostAdvance::FDelegate::CreateUObject(this, &UClientPredictionComponent::PrePhysicsAdvance));
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	Solver->RemovePostAdvanceCallback(OnPhysicsAdvancedDelegate);
}

void UClientPredictionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
	check(UpdatedComponent);
}

void UClientPredictionComponent::PrePhysicsAdvance(Chaos::FReal Dt) {
	if (!Timestep) {
		Timestep = Dt;
	} else {
		// It's expected that the timestep is always constant
		checkSlow(Timestep == Dt);
	}

	Model->PreTick(Dt, ForceSimulationFrames != 0, UpdatedComponent, GetOwnerRole());
}

void UClientPredictionComponent::OnPhysicsAdvanced(Chaos::FReal Dt) {
	if (ForceSimulationFrames) {
		GetWorld()->GetPhysicsScene()->GetSolver()->UpdateGameThreadStructures();
		
		--ForceSimulationFrames;
	}
	
	Model->PostTick(Dt, ForceSimulationFrames != 0, UpdatedComponent, GetOwnerRole());
}

void UClientPredictionComponent::ForceSimulate(uint32 Frames) {
	ForceSimulationFrames = Frames;

	auto Solver = GetWorld()->GetPhysicsScene()->GetSolver();

	// These will be enqueued to the physics thread, it won't be blocking.
	for (uint32 i = 0; i < Frames; i++) {
		Solver->AdvanceAndDispatch_External(Timestep);
	}
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
	Model->ReceiveInputPacket(Proxy);
}
void UClientPredictionComponent::RecvServerState_Implementation(FNetSerializationProxy Proxy) {
	Model->ReceiveAuthorityState(Proxy);
}
