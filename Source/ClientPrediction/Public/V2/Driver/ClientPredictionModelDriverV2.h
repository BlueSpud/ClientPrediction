﻿#pragma once

#include "ClientPredictionNetSerialization.h"
#include "V2/World/ClientPredictionTickCallback.h"

namespace ClientPrediction {
	class IModelDriverDelegate {
	public:
		virtual ~IModelDriverDelegate() = default;

		virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
	};

	// TODO Consider creating a common interface between the model and the driver
	class IModelDriver : public ITickCallback  {
	public:
		virtual ~IModelDriver() override = default;

		// Input packet / state receiving
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) {}
		virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {}
	};

}
