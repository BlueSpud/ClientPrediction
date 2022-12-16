#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "Driver/ClientPredictionModelDriver.h"

#include "Driver/Drivers/ClientPredictionModelAuthDriver.h"
#include "Driver/Drivers/ClientPredictionModelAutoProxyDriver.h"
#include "World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	// Delegate
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct IPhysicsModelDelegate {
		virtual ~IPhysicsModelDelegate() = default;

		virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
	};

	// Interface
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct FPhysicsModelBase {
		virtual ~FPhysicsModelBase() = default;

		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) = 0;
		virtual void Cleanup() = 0;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) = 0;
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const = 0;
	};

	// Sim output
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename StateType>
	struct FSimOutput {
		explicit FSimOutput(FPhysicsState<StateType>& PhysState)
			: PhysState(PhysState) {}

		StateType& State() const { return PhysState.Body; }

	private:
		FPhysicsState<StateType>& PhysState;
	};


	// Model declaration
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename InputType, typename StateType>
	struct FPhysicsModel : public FPhysicsModelBase, public IModelDriverDelegate<InputType, StateType> {
		using SimOutput = FSimOutput<StateType>;

		/**
		 * Simulates the model before physics has been run for this ticket.
		 * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
		 * game thread need to be properly synchronized.
		 */
		virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

		/**
		 * Simulates the model after physics has been run for this ticket.
		 * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
		 * game thread need to be properly synchronized.
		 */
		virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

		// FPhysicsModelBase
		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) override final;
		virtual void Finalize(const StateType& State, Chaos::FReal Dt) override final;

		virtual ~FPhysicsModel() override = default;
		virtual void Cleanup() override final;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override final;
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const override final;

		// IModelDriverDelegate
		virtual void GenerateInitialState(FPhysicsState<StateType>& State) override final;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) override final;
		virtual void ProduceInput(FInputPacketWrapper<InputType>& Packet) override final;

		virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) override final;
		virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) override final;

	public:

		DECLARE_DELEGATE_OneParam(FPhysicsModelProduceInput, InputType&)
		FPhysicsModelProduceInput ProduceInputDelegate;

		DECLARE_DELEGATE_TwoParams(FPhysicsModelFinalize, const StateType&, Chaos::FReal Dt)
		FPhysicsModelFinalize FinalizeDelegate;

	private:
		class UPrimitiveComponent* CachedComponent = nullptr;
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver<InputType>> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};

	// Implementation
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
		CachedComponent = Component;
		check(CachedComponent);

		Delegate = InDelegate;
		check(Delegate);

		const UWorld* World  = CachedComponent->GetWorld();
		check(World);

		CachedWorldManager = FWorldManager::ManagerForWorld(World);
		check(CachedWorldManager)
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::Finalize(const StateType& State, Chaos::FReal Dt) { FinalizeDelegate.ExecuteIfBound(State, Dt); }

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::Cleanup() {
		if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		CachedWorldManager = nullptr;
		ModelDriver = nullptr;
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::SetNetRole(ENetRole Role, bool bShouldTakeInput,
		FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
		check(CachedWorldManager)
		check(CachedComponent)
		check(Delegate)

		if (ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		int32 RewindBufferSize = CachedWorldManager->GetRewindBufferSize();
		switch (Role) {
		case ROLE_Authority:
			// TODO pass bShouldTakeInput here
			ModelDriver = MakeUnique<FModelAuthDriver<InputType, StateType>>(CachedComponent, this, AutoProxyRep, SimProxyRep, RewindBufferSize);
			CachedWorldManager->AddTickCallback(ModelDriver.Get());
			break;
		case ROLE_AutonomousProxy: {
			auto NewDriver = MakeUnique<FModelAutoProxyDriver<InputType, StateType>>(CachedComponent, this, AutoProxyRep, RewindBufferSize);
			CachedWorldManager->AddTickCallback(NewDriver.Get());
			CachedWorldManager->AddRewindCallback(NewDriver.Get());
			ModelDriver = MoveTemp(NewDriver);
		} break;
		case ROLE_SimulatedProxy:
			// TODO add in the sim proxy
			break;
		default:
			break;
		}
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::ReceiveInputPackets(FNetSerializationProxy& Proxy) const {
		if (ModelDriver == nullptr) { return; }

		TArray<FInputPacketWrapper<InputType>> Packets;
		Proxy.NetSerializeFunc = [&](FArchive& Ar) { Ar << Packets; };

		Proxy.Deserialize();
		ModelDriver->ReceiveInputPackets(Packets);
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) {
		check(Delegate);

		FNetSerializationProxy Proxy;
		Proxy.NetSerializeFunc = [=](FArchive& Ar) mutable { Ar << Packets; };
		Delegate->EmitInputPackets(Proxy);
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::ProduceInput(FInputPacketWrapper<InputType>& Packet) {
		ProduceInputDelegate.ExecuteIfBound(Packet.Body);
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) {
		SimOutput Output(OutState);
		SimulatePrePhysics(Dt, Context, Input, PrevState.Body, Output);
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) {
		SimOutput Output(OutState);
		SimulatePostPhysics(Dt, Context, Input, PrevState.Body, Output);
	}

	template <typename InputType, typename StateType>
	void FPhysicsModel<InputType, StateType>::GenerateInitialState(FPhysicsState<StateType>& State) {
		State = {};

		// TODO Actually generate initial state
	}
}
