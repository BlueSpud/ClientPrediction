#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../Input.h"

template <typename InputPacket, typename ModelState>
class ClientPredictionAuthorityDriver : public IClientPredictionModelDriver<InputPacket, ModelState> {

public:

	ClientPredictionAuthorityDriver();

	// Simulation ticking
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override;

private:
	
	static constexpr uint32 kSyncFrames = 5;
	static constexpr uint32 kAuthorityTargetInputBufferSize = 3;
	
	/** The index of the next frame on both the remotes and authority */
	uint32 NextLocalFrame = 0;

	/** Autonomous proxy index for the next input packet number */
	uint32 NextInputPacket = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacketIdx = kInvalidFrame;
	FInputPacketWrapper<InputPacket> CurrentInputPacket;

	FModelStateWrapper<ModelState> CurrentState;
	ModelState LastState;

	FInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;
};

template <typename InputPacket, typename ModelState>
ClientPredictionAuthorityDriver<InputPacket, ModelState>::ClientPredictionAuthorityDriver() {
	InputBuffer.SetAuthorityTargetBufferSize(kAuthorityTargetInputBufferSize);
}

template <typename InputPacket, typename ModelState>
void ClientPredictionAuthorityDriver<InputPacket, ModelState>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	LastState = CurrentState.State;

	// Pre-tick
	if (CurrentInputPacketIdx != kInvalidFrame || InputBuffer.AuthorityBufferSize() > InputBuffer.GetAuthorityTargetBufferSize()) {
		InputBuffer.ConsumeInputAuthority(CurrentInputPacket);
		CurrentInputPacketIdx = CurrentInputPacket.PacketNumber;
	}
	
	CurrentState = FModelStateWrapper<ModelState>();

	// Tick
	Simulate(Dt, Component, LastState, CurrentState.State, CurrentInputPacket.Packet);

	// Post-tick
	if (NextLocalFrame % kSyncFrames) {
		EmitAuthorityState.CheckCallable();

		// Capture by value here so that the proxy stores the state with it
		FNetSerializationProxy Proxy([=](FArchive& Ar) mutable {
			CurrentState.NetSerialize(Ar);
		});
		
		EmitAuthorityState(Proxy);
	}
	
}

template <typename InputPacket, typename ModelState>
ModelState ClientPredictionAuthorityDriver<InputPacket, ModelState>::GenerateOutput(Chaos::FReal Alpha) {
	ModelState InterpolatedState = LastState;
	InterpolatedState.Interpolate(Alpha, CurrentState.State);
	return InterpolatedState;
}

template <typename InputPacket, typename ModelState>
void ClientPredictionAuthorityDriver<InputPacket, ModelState>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	TArray<FInputPacketWrapper<InputPacket>> Packets;
	Proxy.NetSerializeFunc = [&Packets](FArchive& Ar) {
		Ar << Packets;
	};

	Proxy.Deserialize();
	for (const FInputPacketWrapper<InputPacket>& Packet : Packets) {
		InputBuffer.QueueInputAuthority(Packet);
	}
}

template <typename InputPacket, typename ModelState>
void ClientPredictionAuthorityDriver<InputPacket, ModelState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	// No-op, since the authority should never receive a state from the authority 
}
