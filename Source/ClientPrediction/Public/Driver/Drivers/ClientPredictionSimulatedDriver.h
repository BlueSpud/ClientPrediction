#pragma once
#include "Driver/ClientPredictionModelDriver.h"

namespace ClientPrediction {

	template <typename InputType, typename StateType>
	class FSimulatedModelDriver : public IModelDriver<InputType> {

	public:
		FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate);

	protected:
		class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

		void PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt);
		void PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time);

	protected:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;

		FInputPacketWrapper<InputType> CurrentInput{}; // Only used on physics thread
		FPhysicsState<StateType> CurrentState{}; // Only used on physics thread
		FPhysicsState<StateType> LastState{}; // Only used on physics thread
	};

	template <typename InputType, typename StateType>
	FSimulatedModelDriver<InputType, StateType>::FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate)
		: UpdatedComponent(UpdatedComponent),
		  Delegate(Delegate) {
		check(UpdatedComponent);
		check(Delegate);

		Delegate->GenerateInitialState(CurrentState);
		LastState = CurrentState;
	}

	template <typename InputType, typename StateType>
	Chaos::FRigidBodyHandle_Internal* FSimulatedModelDriver<InputType, StateType>::GetPhysicsHandle() const {
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		check(BodyInstance)

		Chaos::FRigidBodyHandle_Internal* PhysicsHandle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		check(PhysicsHandle);

		return PhysicsHandle;
	}

	template <typename InputType, typename StateType>
	void FSimulatedModelDriver<InputType, StateType>::PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt) {
		LastState = CurrentState;

		CurrentState = {};
		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = CurrentInput.PacketNumber;

		auto* Handle = GetPhysicsHandle();
		FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePrePhysics(Dt, Context, CurrentInput.Body, LastState, CurrentState);
	}

	template <typename InputType, typename StateType>
	void FSimulatedModelDriver<InputType, StateType>::PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		const auto Handle = GetPhysicsHandle();
		CurrentState.FillState(Handle);

		const FPhysicsContext Context(Handle, UpdatedComponent);
		Delegate->SimulatePostPhysics(Dt, Context, CurrentInput.Body, LastState, CurrentState);
	}
}
