#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../Input.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionAuthorityDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;
	
public:

	ClientPredictionAuthorityDriver();

	// Simulation ticking
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override;

private:
	
	uint32 NextFrame = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacketIdx = kInvalidFrame;
	FInputPacketWrapper<InputPacket> CurrentInputPacket;
	
	WrappedState CurrentState;
	ModelState LastState;

	FInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;
};

template <typename InputPacket, typename ModelState, typename CueSet>
ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ClientPredictionAuthorityDriver() {
	InputBuffer.SetAuthorityTargetBufferSize(kAuthorityTargetInputBufferSize);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	LastState = CurrentState.State;

	// Pre-tick
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.Cues.Empty();
	BeginTick(Dt, CurrentState.State, Component);
	
	if (CurrentInputPacketIdx != kInvalidFrame || InputBuffer.AuthorityBufferSize() > InputBuffer.GetAuthorityTargetBufferSize()) {
		InputBuffer.ConsumeInputAuthority(CurrentInputPacket);
		CurrentInputPacketIdx = CurrentInputPacket.PacketNumber;
	}
	
	CurrentState.InputPacketNumber = CurrentInputPacketIdx;
	
	// Tick
	FSimulationOutput<ModelState, CueSet> Output(CurrentState);
	Simulate(Dt, Component, LastState, Output, CurrentInputPacket.Packet);

	for (int i = 0; i < CurrentState.Cues.Num(); i++) {
		HandleCue(CurrentState.State, static_cast<CueSet>(CurrentState.Cues[i]));
	}
	
	if (NextFrame % kSyncFrames == 0) {
		EmitAuthorityState.CheckCallable();

		// Capture by value here so that the proxy stores the state with it
		FNetSerializationProxy Proxy([=](FArchive& Ar) mutable {
			CurrentState.NetSerialize(Ar);
		});
		
		EmitAuthorityState(Proxy);
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
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	// No-op, since the authority should never receive a state from the authority 
}
