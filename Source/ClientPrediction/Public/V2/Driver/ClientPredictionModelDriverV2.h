#pragma once

#include "ClientPredictionNetSerialization.h"
#include "V2/World/ClientPredictionTickCallback.h"
#include "V2/ClientPredictionInput.h"

namespace ClientPrediction {
	class IModelDriverDelegate {
	public:
		virtual ~IModelDriverDelegate() = default;

		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Proxy) = 0;
		virtual void ProduceInput(FInputPacketWrapper& Packet) = 0;
	};

	class IModelDriver : public ITickCallback  {
	public:
		virtual ~IModelDriver() override = default;

		// Input packet / state receiving
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) {}
		virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {}
	};

}
