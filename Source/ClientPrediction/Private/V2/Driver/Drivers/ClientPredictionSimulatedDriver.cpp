#include "V2/Driver/Drivers/ClientPredictionSimulatedDriver.h"

#include "Declares.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	FSimulatedModelDriver::FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* Delegate)
		: UpdatedComponent(UpdatedComponent),
		  Delegate(Delegate) {
		check(UpdatedComponent);
		check(Delegate);

		Delegate->GenerateInitialState(CurrentState);
		LastState = CurrentState;
	}

	Chaos::FRigidBodyHandle_Internal* FSimulatedModelDriver::GetPhysicsHandle() const {
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		check(BodyInstance)

		Chaos::FRigidBodyHandle_Internal* PhysicsHandle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		check(PhysicsHandle);

		return PhysicsHandle;
	}

	void FSimulatedModelDriver::PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt) {
		LastState = MoveTemp(CurrentState);
		Delegate->NewState(CurrentState);

		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = CurrentInput.PacketNumber;

		auto* Handle = GetPhysicsHandle();
		FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePrePhysics(kFixedDt, Context, CurrentInput.Body.Get(), LastState, CurrentState);
	}

	void FSimulatedModelDriver::PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		const auto Handle = GetPhysicsHandle();
		CurrentState.FillState(Handle);

		const FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePostPhysics(Dt, Context, CurrentInput.Body.Get(), LastState, CurrentState);
	}
}
