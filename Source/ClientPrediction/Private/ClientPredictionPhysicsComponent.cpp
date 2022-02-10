#include "ClientPredictionPhysicsComponent.h"

#include "ClientPrediction.h"
#include "PBDRigidsSolver.h"

UClientPredictionPhysicsComponent::UClientPredictionPhysicsComponent() {
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UClientPredictionPhysicsComponent::BeginPlay() {
	Super::BeginPlay();
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	OnPhysicsAdvancedDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateUObject(this, &UClientPredictionPhysicsComponent::OnPhysicsAdvanced));
}

void UClientPredictionPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	Solver->RemovePostAdvanceCallback(OnPhysicsAdvancedDelegate);
}

void UClientPredictionPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	while (!QueuedClientSendState.IsEmpty()) {
		FPhysicsState State;
		QueuedClientSendState.Dequeue(State);
		
		RecvServerState(State);
	}
}

void UClientPredictionPhysicsComponent::OnRegister() {
	Super::OnRegister();

	// TODO make this more sophisticated
	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	check(UpdatedComponent);
}

void UClientPredictionPhysicsComponent::OnPhysicsAdvanced(float Dt) {
	if (!Timestep) {
		Timestep = Dt;
	} else {
		// It's expected that the timestep is always constant
		checkSlow(Timestep == Dt);
	}

	const ENetRole OwnerRole = GetOwnerRole();
	// TODO make this work
	// switch (OwnerRole) {
	// case ENetRole::ROLE_Authority:
	// 	OnPhysicsAdvancedAuthority();
	// 	break;
	// case ENetRole::ROLE_AutonomousProxy:
	// 	OnPhysicsAdvancedAutonomousProxy();
	// 	break;
	// default:
	// 	return;
	// }
	if (GetOwner()->HasAuthority()) {
		OnPhysicsAdvancedAuthority();
	} else {
		OnPhysicsAdvancedAutonomousProxy();
	}
}

void UClientPredictionPhysicsComponent::OnPhysicsAdvancedAuthority() {
	checkSlow(ForceSimulateFrames == 0) // Server should never resimulate / fast-forward
	
	FBodyInstance* Body = UpdatedComponent->GetBodyInstance();
	Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
	if (!Handle) {
		return;
	}
	
	if (NextLocalFrame % SyncFrames) {
		if (Handle) {
			QueuedClientSendState.Enqueue(FPhysicsState(Handle, NextLocalFrame));
		}
	}

	++NextLocalFrame;
}

void UClientPredictionPhysicsComponent::OnPhysicsAdvancedAutonomousProxy() {
	FBodyInstance* Body = UpdatedComponent->GetBodyInstance();
	Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
	if (!Handle) {
		return;
	}
	
	FPhysicsState CurrentState(Handle, NextLocalFrame++);
	ClientHistory.Enqueue(CurrentState);

	// If there are frames that are being used to fast-forward/resimulate no logic needs to be performed
	// for them/
	if (ForceSimulationFrames) {
		GetWorld()->GetPhysicsScene()->GetSolver()->UpdateGameThreadStructures();
		
		--ForceSimulationFrames;
		return;
	}
	
	FPhysicsState LocalLastServerState = LastServerState;
	
	if (LocalLastServerState.FrameNumber == FPhysicsState::kInvalidFrame) {
		// Never received a frame from the server
		return;
	}

	if (LocalLastServerState.FrameNumber <= AckedServerFrame && AckedServerFrame != FPhysicsState::kInvalidFrame) {
		// Last state received from the server was already acknowledged
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Processing frame %i from the server"), LocalLastServerState.FrameNumber);
	if (LocalLastServerState.FrameNumber > CurrentState.FrameNumber) {
		// Server is ahead of the client. The client should just chuck out everything and resimulate
		Rewind(LocalLastServerState, Handle);
		UE_LOG(LogTemp, Warning, TEXT("Client was behind server. Jumping to frame %i and resimulating"), LocalLastServerState.FrameNumber);
		
		ForceSimulate(ClientForwardPredictionFrames);
	} else {
		// Check history against the server state
		FPhysicsState HistoricState;
		bool bFound = false;
		
		while (!ClientHistory.IsEmpty()) {
			ClientHistory.Dequeue(HistoricState);
			if (HistoricState.FrameNumber == LocalLastServerState.FrameNumber) {
				bFound = true;
				break;
			}
		}

		check(bFound);

		if (HistoricState == LocalLastServerState) {
			// Server state and historic state matched, simulation was good up to LocalServerState.FrameNumber
			AckedServerFrame = LocalLastServerState.FrameNumber;
			UE_LOG(LogTemp, Log, TEXT("Acked up to %i"), AckedServerFrame);
		} else {
			// Server/client mismatch. Resimulate the client
			Rewind(LocalLastServerState, Handle);
			UE_LOG(LogTemp, Warning, TEXT("Rewinding and resimulating from %i"), LocalLastServerState.FrameNumber);
			
			// TODO simulate back to the present, not just the distance from the server
			ForceSimulate(ClientForwardPredictionFrames);
		}
		
	}
}

void UClientPredictionPhysicsComponent::Rewind(FPhysicsState& State, Chaos::FRigidBodyHandle_Internal* Handle) {
	State.Rewind(Handle);

	ClientHistory.Empty();
	AckedServerFrame = State.FrameNumber;
	
	// Add here because the body is at State.FrameNumber so the next frame will be State.FrameNumber + 1
	NextLocalFrame = State.FrameNumber + 1;
}

void UClientPredictionPhysicsComponent::ForceSimulate(uint32 Frames) {
	ForceSimulationFrames = Frames;

	auto Solver = GetWorld()->GetPhysicsScene()->GetSolver();

	// These will be enqueued to the physics thread, it won't be blocking.
	for (uint32 i = 0; i < Frames; i++) {
		Solver->AdvanceAndDispatch_External(Timestep);
	}
}

void UClientPredictionPhysicsComponent::RecvServerState_Implementation(FPhysicsState State) { LastServerState = State; }
