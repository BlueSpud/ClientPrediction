#include "PhysicsComponent.h"

#include "PBDRigidsSolver.h"

void UPhysicsComponent::BeginPlay() {
	Super::BeginPlay();
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	OnPhysicsAdvancedDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateUObject(this, &UPhysicsComponent::OnPhysicsAdvanced));
}

void UPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);
	
	Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
	Solver->RemovePostAdvanceCallback(OnPhysicsAdvancedDelegate);
}

void UPhysicsComponent::OnRegister() {
	Super::OnRegister();

	// TODO make this more sophisticated
	UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	check(UpdatedComponent);
}

void UPhysicsComponent::OnPhysicsAdvanced(float Dt) {
	if (!Timestep) {
		Timestep = Dt;
	} else {
		// It's expected that the timestep is always constant
		checkSlow(Timestep == Dt);
	}

	const ENetRole OwnerRole = GetOwnerRole();
	switch (OwnerRole) {
	case ENetRole::ROLE_Authority:
		OnPhysicsAdvancedAuthority();
	case ENetRole::ROLE_AutonomousProxy:
		OnPhysicsAdvancedAuthority();
	default:
		return;
	}
}

void UPhysicsComponent::OnPhysicsAdvancedAutonomousProxy() {
	if (NextLocalFrame % SyncFrames) {
		FBodyInstance* Body = UpdatedComponent->GetBodyInstance();
		Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();

		if (Handle) {
			RecvServerState(FPhysicsState(Handle, NextLocalFrame));
		}
	}

	++NextLocalFrame;
}

void UPhysicsComponent::OnPhysicsAdvancedAuthority() {
	
}

void UPhysicsComponent::RecvServerState_Implementation(FPhysicsState State) {
	LastServerState = State;
}
