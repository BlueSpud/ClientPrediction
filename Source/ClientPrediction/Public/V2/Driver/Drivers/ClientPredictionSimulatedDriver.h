#pragma once
#include "V2/Driver/ClientPredictionModelDriverV2.h"

namespace ClientPrediction {

	template <typename StateType>
	class FSimulatedModelDriver : public IModelDriver {

	public:
		FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<StateType>* Delegate);

	protected:
		class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

		void PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt);
		void PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time);

	protected:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate<StateType>* Delegate = nullptr;

		FInputPacketWrapper CurrentInput{}; // Only used on physics thread
		FPhysicsState<StateType> CurrentState{}; // Only used on physics thread
		FPhysicsState<StateType> LastState{}; // Only used on physics thread
	};

	template <typename StateType>
	FSimulatedModelDriver<StateType>::FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<StateType>* Delegate)
		: UpdatedComponent(UpdatedComponent),
		  Delegate(Delegate) {
		check(UpdatedComponent);
		check(Delegate);

		Delegate->GenerateInitialState(CurrentState);
		LastState = CurrentState;
	}

	template <typename StateType>
	Chaos::FRigidBodyHandle_Internal* FSimulatedModelDriver<StateType>::GetPhysicsHandle() const {
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		check(BodyInstance)

		Chaos::FRigidBodyHandle_Internal* PhysicsHandle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		check(PhysicsHandle);

		return PhysicsHandle;
	}

	template <typename StateType>
	void FSimulatedModelDriver<StateType>::PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt) {
		LastState = CurrentState;

		CurrentState = {};
		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = CurrentInput.PacketNumber;

		auto* Handle = GetPhysicsHandle();
		FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePrePhysics(Dt, Context, CurrentInput.Body.Get(), LastState, CurrentState);
	}

	template <typename StateType>
	void FSimulatedModelDriver<StateType>::PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		const auto Handle = GetPhysicsHandle();
		CurrentState.FillState(Handle);

		const FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePostPhysics(Dt, Context, CurrentInput.Body.Get(), LastState, CurrentState);
	}
}
