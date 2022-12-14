#pragma once

#include "ClientPredictionNetSerialization.h"
#include "V2/World/ClientPredictionTickCallback.h"
#include "V2/Physics/ClientPredictionPhysicsContextV2.h"
#include "V2/ClientPredictionInput.h"
#include "V2/ClientPredictionModelTypesV2.h"

namespace ClientPrediction {
	class IModelDriverDelegate {
	public:
		virtual ~IModelDriverDelegate() = default;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Proxy) = 0;
		virtual void ProduceInput(FInputPacketWrapper& Packet) = 0;

		virtual void SimulatePrePhysics(void* Packet, FPhysicsContext& Context) = 0;
		virtual void SimulatePostPhysics(void* Packet, const FPhysicsContext& Context) = 0;

		virtual void GenerateInitialState(FPhysicsState& State) = 0;
		virtual void NewState(FPhysicsState& State) = 0;
		virtual void NetSerialize(FPhysicsState& State, FArchive& Ar) = 0;
	};

	class IModelDriver : public ITickCallback  {
	public:
		virtual ~IModelDriver() override = default;

		// Input packet / state receiving
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) {}
		virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {}
	};

}
