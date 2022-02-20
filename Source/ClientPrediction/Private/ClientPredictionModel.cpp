#include "ClientPredictionModel.h"

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::NetSerializeInputPacket(FArchive& Ar) {


	
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::NetSerializeStatePacket(FArchive& Ar) {

	
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::PreTick(Chaos::FReal Dt, bool bIsForcedSimulation,
	UPrimitiveComponent* Component, ENetRole Role) {

	
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::PostTick(Chaos::FReal Dt, bool bIsForcedSimulation,
	UPrimitiveComponent* Component, ENetRole Role) {

	
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::ReceiveInputPacket(FNetSerializationProxy& Proxy) {
	InputPacket Packet;
	Proxy.NetSerializeFunc = [&Packet](FArchive& Ar) {
		Packet.NetSerialize(Ar);	
	};

	Proxy.Deserialize();
	InputBuffer.QueueInputAuthority(Packet);
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	SimulationState State;
	Proxy.NetSerializeFunc = [&State](FArchive& Ar) {
		State.NetSerialize(Ar);	
	};

	Proxy.Deserialize();
	LastAuthorityState = State;
}

template <typename InputPacket, typename SimulationState>
void BaseClientPredictionModel<InputPacket, SimulationState>::Rewind(const SimulationState& State,
	UPrimitiveComponent* Component) const {
	State.Rewind(Component);
}
