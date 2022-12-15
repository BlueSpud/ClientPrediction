#pragma once

#include "ClientPredictionNetSerialization.h"
#include "World/ClientPredictionTickCallback.h"
#include "Physics/ClientPredictionPhysicsContext.h"
#include "ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {

	template <typename StateType>
	class IModelDriverDelegate {
	public:
		virtual ~IModelDriverDelegate() = default;

		virtual void GenerateInitialState(FPhysicsState<StateType>& State) = 0;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Proxy) = 0;
		virtual void ProduceInput(FInputPacketWrapper& Packet) = 0;

		virtual void SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) = 0;
		virtual void SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context, void* Input, const FPhysicsState<StateType>& PrevState, FPhysicsState<StateType>& OutState) = 0;
	};

	class IModelDriver : public ITickCallback  {
	public:
		virtual ~IModelDriver() override = default;

		// Input packet / state receiving
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) {}
		virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {}
	};

}
