#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"

#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"
#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"
#include "V2/World/ClientPredictionWorldManager.h"

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

	template <typename InputPacketType, typename StateType>
	struct FPhysicsModel : public FPhysicsModelBase, public IModelDriverDelegate<StateType> {
		using SimOutput = FSimOutput<StateType>;

		/**
		 * Simulates the model before physics has been run for this ticket.
		 * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
		 * game thread need to be properly synchronized.
		 */
		virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputPacketType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

		/**
		 * Simulates the model after physics has been run for this ticket.
		 * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
		 * game thread need to be properly synchronized.
		 */
		virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputPacketType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

		// FPhysicsModelBase
		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) override final;

		virtual ~FPhysicsModel() override = default;
		virtual void Cleanup() override final;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override final;
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const override final;

		// IModelDriverDelegate
		virtual void GenerateInitialState(FPhysicsState<StateType>& State) override final;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Packets) override final;
		virtual void ProduceInput(FInputPacketWrapper& Packet) override final;

		virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) override final;
		virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) override final;

	public:

		DECLARE_DELEGATE_OneParam(FPhysicsModelProduceInput, InputPacketType&)
		FPhysicsModelProduceInput ProduceInputDelegate;

	private:
		class UPrimitiveComponent* CachedComponent = nullptr;
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};

	// Implementation
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
		CachedComponent = Component;
		check(CachedComponent);

		Delegate = InDelegate;
		check(Delegate);

		const UWorld* World  = CachedComponent->GetWorld();
		check(World);

		CachedWorldManager = FWorldManager::ManagerForWorld(World);
		check(CachedWorldManager)
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::Cleanup() {
		if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		CachedWorldManager = nullptr;
		ModelDriver = nullptr;
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::SetNetRole(ENetRole Role, bool bShouldTakeInput,
		FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
		check(CachedWorldManager)
		check(CachedComponent)
		check(Delegate)

		if (ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		switch (Role) {
		case ROLE_Authority:
			// TODO pass bShouldTakeInput here
			ModelDriver = MakeUnique<FModelAuthDriver<StateType>>(CachedComponent, this, AutoProxyRep, SimProxyRep);
			CachedWorldManager->AddTickCallback(ModelDriver.Get());
			break;
		case ROLE_AutonomousProxy: {
			auto NewDriver = MakeUnique<FModelAutoProxyDriver<StateType>>(CachedComponent, this, AutoProxyRep, CachedWorldManager->GetRewindBufferSize());
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

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::ReceiveInputPackets(FNetSerializationProxy& Proxy) const {
		if (ModelDriver == nullptr) { return; }

		TArray<FInputPacketWrapper> Packets;
		Proxy.NetSerializeFunc = [&](FArchive& Ar) {
			uint8 Size;
			Ar << Size;

			for (uint8 i = 0; i < Size; i++) {
				Packets.Emplace();
				FInputPacketWrapper& Packet = Packets.Last();
				Packet.NetSerialize(Ar);

				TSharedPtr<InputPacketType> Body = MakeShared<InputPacketType>();
				Body->NetSerialize(Ar);
				Packet.Body = MoveTemp(Body);
			}
		};

		Proxy.Deserialize();
		ModelDriver->ReceiveInputPackets(Packets);
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::EmitInputPackets(TArray<FInputPacketWrapper>& Packets) {
		check(Delegate);

		FNetSerializationProxy Proxy;
		Proxy.NetSerializeFunc = [=](FArchive& Ar) mutable {
			uint8 Size = Packets.Num();
			Ar << Size;

			for (uint8 i = 0; i < Size; i++) {
				Packets[i].NetSerialize(Ar);

				check(Packets[i].Body)
				static_cast<InputPacketType*>(Packets[i].Body.Get())->NetSerialize(Ar);
			}
		};


		Delegate->EmitInputPackets(Proxy);
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::ProduceInput(FInputPacketWrapper& Packet) {
		TSharedPtr<InputPacketType> Body = MakeShared<InputPacketType>();
		ProduceInputDelegate.ExecuteIfBound(*Body.Get());

		Packet.Body = MoveTemp(Body);
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) {
		InputPacketType* InputBody = static_cast<InputPacketType*>(Input);

		SimOutput Output(OutState);
		SimulatePrePhysics(Dt, Context, *InputBody, PrevState.Body, Output);
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) {
		InputPacketType* InputBody = static_cast<InputPacketType*>(Input);

		SimOutput Output(OutState);
		SimulatePostPhysics(Dt, Context, *InputBody, PrevState.Body, Output);
	}

	template <typename InputPacketType, typename StateType>
	void FPhysicsModel<InputPacketType, StateType>::GenerateInitialState(FPhysicsState<StateType>& State) {
		State = {};

		// TODO Actually generate initial state
	}
}
