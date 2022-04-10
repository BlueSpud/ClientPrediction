#pragma once

#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../Input.h"

template <typename InputPacket, typename ModelState>
class ClientPredictionSimProxyDriver : public IClientPredictionModelDriver<InputPacket, ModelState> {

public:

	ClientPredictionSimProxyDriver() = default;

	// Simulation ticking
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override;

private:
	
	FModelStateWrapper<ModelState> LastAuthorityState;
	FModelStateWrapper<ModelState> CurrentState;
	ModelState LastState;
};

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	LastState = CurrentState.State;
	CurrentState = LastAuthorityState;
}

template <typename InputPacket, typename ModelState>
ModelState ClientPredictionSimProxyDriver<InputPacket, ModelState>::GenerateOutput(Chaos::FReal Alpha) {
	ModelState InterpolatedState = LastState;
	InterpolatedState.Interpolate(Alpha, CurrentState.State);
	return InterpolatedState;
}

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	// No-op, sim proxy should never get input
}

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	FModelStateWrapper<ModelState> State;
	Proxy.NetSerializeFunc = [&State](FArchive& Ar) {
		State.NetSerialize(Ar);	
	};

	Proxy.Deserialize();
	LastAuthorityState = State;
}
