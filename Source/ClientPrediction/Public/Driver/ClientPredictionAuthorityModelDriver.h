#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../Input.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionAuthorityDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;

public:

	ClientPredictionAuthorityDriver(bool bTakesInput);

	// Simulation ticking

	virtual void Initialize() override;
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void BindToRepProxies(FClientPredictionRepProxy& NewAutoProxyRep, FClientPredictionRepProxy& NewSimProxyRep) override;

private:

	uint32 NextFrame = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacketIdx = kInvalidFrame;
	FInputPacketWrapper<InputPacket> CurrentInputPacket;

	WrappedState CurrentState;
	ModelState LastState;

	bool bTakesInput = false;
	FInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;

	FClientPredictionRepProxy* AutoProxyRep = nullptr;
	FClientPredictionRepProxy* SimProxyRep = nullptr;
};

template <typename InputPacket, typename ModelState, typename CueSet>
ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ClientPredictionAuthorityDriver(bool bTakesInput) : bTakesInput(bTakesInput) {
	InputBuffer.SetAuthorityTargetBufferSize(kAuthorityTargetInputBufferSize);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Initialize() {
	GenerateInitialState(CurrentState.State);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	LastState = CurrentState.State;

	// Pre-tick
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.Cues.Empty();
	BeginTick(Dt, CurrentState.State, Component);

	if (!bTakesInput) {
		if (CurrentInputPacketIdx != kInvalidFrame || InputBuffer.AuthorityBufferSize() > InputBuffer.GetAuthorityTargetBufferSize()) {
			InputBuffer.ConsumeInputAuthority(CurrentInputPacket);
			CurrentInputPacketIdx = CurrentInputPacket.PacketNumber;
		}

		CurrentState.InputPacketNumber = CurrentInputPacketIdx;
	} else {
		CurrentInputPacket = FInputPacketWrapper<InputPacket>();
		InputDelegate.ExecuteIfBound(CurrentInputPacket.Packet, CurrentState.State, Dt);
	}

	// Tick
	FSimulationOutput<ModelState, CueSet> Output(CurrentState);
	Simulate(Dt, Component, LastState, Output, CurrentInputPacket.Packet);

	for (int i = 0; i < CurrentState.Cues.Num(); i++) {
		HandleCue(CurrentState.State, static_cast<CueSet>(CurrentState.Cues[i]));
	}

	if (CurrentState.Cues.IsEmpty() || !EmitReliableAuthorityState) {
		if (AutoProxyRep != nullptr) { AutoProxyRep->Dispatch(); }
		if (SimProxyRep != nullptr) { SimProxyRep->Dispatch(); }
	} else {

		// Reliably dispatch the state
		FNetSerializationProxy Proxy;
		Proxy.NetSerializeFunc = [=](FArchive& Ar) {
			CurrentState.NetSerialize(Ar);
		};

		EmitReliableAuthorityState(Proxy);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::GenerateOutput(Chaos::FReal Alpha) {
	ModelState InterpolatedState = LastState;
	InterpolatedState.Interpolate(Alpha, CurrentState.State);
	return InterpolatedState;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	TArray<FInputPacketWrapper<InputPacket>> Packets;
	Proxy.NetSerializeFunc = [&Packets](FArchive& Ar) {
		Ar << Packets;
	};

	Proxy.Deserialize();
	for (const FInputPacketWrapper<InputPacket>& Packet : Packets) {
		InputBuffer.QueueInputAuthority(Packet);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::BindToRepProxies(FClientPredictionRepProxy& NewAutoProxyRep, FClientPredictionRepProxy& NewSimProxyRep) {
	AutoProxyRep = &NewAutoProxyRep;
	SimProxyRep = &NewSimProxyRep;

	AutoProxyRep->SerializeFunc = [&](FArchive& Ar) {
		CurrentState.NetSerialize(Ar);
	};

	SimProxyRep->SerializeFunc = [&](FArchive& Ar) {
		CurrentState.NetSerialize(Ar);
	};
}
