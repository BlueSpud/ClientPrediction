#include "ClientPredictionPhysicsComponent.h"

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
	PrePhysicsAdvancedDelegate = Solver->AddPreAdvanceCallback(FSolverPostAdvance::FDelegate::CreateUObject(this, &UClientPredictionPhysicsComponent::PrePhysicsAdvance));
}

void UClientPredictionPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	Solver->RemovePostAdvanceCallback(OnPhysicsAdvancedDelegate);
}

void UClientPredictionPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Send states to the client
	while (!QueuedClientSendState.IsEmpty()) {
		FPhysicsState State;
		QueuedClientSendState.Dequeue(State);
		
		RecvServerState(State);
	}

	// Send 
	while (!InputBufferSendQueue.IsEmpty()) {
		FInputPacket Packet;
		InputBufferSendQueue.Dequeue(Packet);

		RecvInputPacket(Packet);
	}
}

void UClientPredictionPhysicsComponent::OnRegister() {
	Super::OnRegister();

	// TODO make this more sophisticated
	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	check(UpdatedComponent);
}

void UClientPredictionPhysicsComponent::PrePhysicsAdvance(Chaos::FReal Dt) {
	if (!Timestep) {
		Timestep = Dt;
	} else {
		// It's expected that the timestep is always constant
		checkSlow(Timestep == Dt);
	}
	
	// TODO make this work better
	const ENetRole OwnerRole = GetOwnerRole();
	switch (OwnerRole) {
	case ENetRole::ROLE_Authority:
		PrePhysicsAdvanceAuthority();
		break;
	case ENetRole::ROLE_AutonomousProxy:
		PrePhysicsAdvanceAutonomousProxy();
		break;
	default:
		return;
	}
}

void UClientPredictionPhysicsComponent::PrePhysicsAdvanceAutonomousProxy() {
	if (ForceSimulationFrames == 0 || InputBuffer.ClientBufferSize() == 0) {
		FInputPacket Packet(NextInputPacket++);
		InputDelegate.ExecuteIfBound(Packet);
		
		InputBuffer.QueueInputClient(Packet);
		InputBufferSendQueue.Enqueue(Packet);
	}

	// Apply input
	FInputPacket Input;
	check(InputBuffer.ConsumeInputClient(Input));
	CurrentInputPacket = Input.PacketNumber;

	if (Input.bIsApplyingForce) {
		FBodyInstance* Body = UpdatedComponent->GetBodyInstance();
		Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		Handle->AddForce(Chaos::FVec3(0.0, 0.0, 1000000.0));
	}
}

void UClientPredictionPhysicsComponent::PrePhysicsAdvanceAuthority() {
	if (CurrentInputPacket != FPhysicsState::kInvalidFrame || InputBuffer.ServerBufferSize() > 15) {
		FInputPacket Input;
		check(InputBuffer.ConsumeInputServer(Input));
		CurrentInputPacket = Input.PacketNumber;

		if (Input.bIsApplyingForce) {
			FBodyInstance* Body = UpdatedComponent->GetBodyInstance();
			Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
			Handle->AddForce(Chaos::FVec3(0.0, 0.0, 1000000.0));
		}
	}
}

void UClientPredictionPhysicsComponent::OnPhysicsAdvanced(Chaos::FReal Dt) {
	// TODO make this work better
	const ENetRole OwnerRole = GetOwnerRole();
	switch (OwnerRole) {
	case ENetRole::ROLE_Authority:
		OnPhysicsAdvancedAuthority();
		break;
	case ENetRole::ROLE_AutonomousProxy:
		OnPhysicsAdvancedAutonomousProxy();
		break;
	default:
		return;
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
			QueuedClientSendState.Enqueue(FPhysicsState(Handle, NextLocalFrame, CurrentInputPacket));
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
	
	FPhysicsState CurrentState(Handle, NextLocalFrame++, CurrentInputPacket);
	ClientHistory.Enqueue(CurrentState);

	// If there are frames that are being used to fast-forward/resimulate no logic needs to be performed
	// for them
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

	if (LocalLastServerState.InputPacketNumber == FPhysicsState::kInvalidFrame) {
		// Server has not started to consume input, ignore it since the client has been applying input since frame 0
		return;
	}
	
	if (LocalLastServerState.FrameNumber > CurrentState.FrameNumber) {
		// Server is ahead of the client. The client should just chuck out everything and resimulate
		Rewind(LocalLastServerState, Handle);
		UE_LOG(LogTemp, Warning, TEXT("Client was behind server. Jumping to frame %i and resimulating"), LocalLastServerState.FrameNumber);
		
		ForceSimulate(FMath::Max(ClientForwardPredictionFrames, InputBuffer.ClientBufferSize()));
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
			InputBuffer.Ack(LocalLastServerState.InputPacketNumber);
			UE_LOG(LogTemp, Log, TEXT("Acked up to %i, input packet %i. Input buffer had %i elements"), AckedServerFrame, LocalLastServerState.InputPacketNumber, InputBuffer.ClientBufferSize());
		} else {
			// Server/client mismatch. Resimulate the client
			Rewind(LocalLastServerState, Handle);
			UE_LOG(LogTemp, Error, TEXT("Rewinding and resimulating from frame %i which used input packet %i"), LocalLastServerState.FrameNumber, LocalLastServerState.InputPacketNumber);
			
			// TODO simulate back to the present, not just the distance from the server
			ForceSimulate(FMath::Max(ClientForwardPredictionFrames, InputBuffer.ClientBufferSize()));
		}
		
	}
}

void UClientPredictionPhysicsComponent::Rewind(FPhysicsState& State, Chaos::FRigidBodyHandle_Internal* Handle) {
	State.Rewind(Handle);

	ClientHistory.Empty();
	AckedServerFrame = State.FrameNumber;
	
	// Add here because the body is at State.FrameNumber so the next frame will be State.FrameNumber + 1
	NextLocalFrame = State.FrameNumber + 1;

	InputBuffer.Rewind(State.InputPacketNumber);
	CurrentInputPacket = State.InputPacketNumber;
}

void UClientPredictionPhysicsComponent::ForceSimulate(uint32 Frames) {
	ForceSimulationFrames = Frames;

	auto Solver = GetWorld()->GetPhysicsScene()->GetSolver();

	// These will be enqueued to the physics thread, it won't be blocking.
	for (uint32 i = 0; i < Frames; i++) {
		Solver->AdvanceAndDispatch_External(Timestep);
	}
}

void UClientPredictionPhysicsComponent::RecvInputPacket_Implementation(FInputPacket Packet) { InputBuffer.QueueInputServer(Packet); }
void UClientPredictionPhysicsComponent::RecvServerState_Implementation(FPhysicsState State) {
	FPhysicsState LocalLastServerState = LastServerState;
	if (LocalLastServerState.FrameNumber == FPhysicsState::kInvalidFrame || State.FrameNumber > LocalLastServerState.FrameNumber) {
		LastServerState = State;
	}
}
