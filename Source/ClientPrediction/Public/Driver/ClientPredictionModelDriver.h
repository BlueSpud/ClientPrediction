#pragma once

#include "ClientPredictionNetSerialization.h"
#include "World/ClientPredictionTickCallback.h"
#include "Physics/ClientPredictionPhysicsContext.h"
#include "Driver/Input/ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {

	template <typename InputType, typename StateType>
	class IModelDriverDelegate {
	public:
		virtual ~IModelDriverDelegate() = default;

		virtual void GenerateInitialState(FPhysicsState<StateType>& State) = 0;
		virtual void Finalize(const StateType& State) = 0;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) = 0;
		virtual void ProduceInput(FInputPacketWrapper<InputType>& Packet) = 0;

		virtual void SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) = 0;
		virtual void SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) = 0;
	};

	template <typename InputType>
	class IModelDriver : public ITickCallback  {
	public:
		virtual ~IModelDriver() override = default;

		// Input packet / state receiving
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {}
		virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {}
	};

}
