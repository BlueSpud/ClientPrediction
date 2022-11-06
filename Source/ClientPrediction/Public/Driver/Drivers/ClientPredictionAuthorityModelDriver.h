#pragma once

#include "../ClientPredictionModelDriver.h"
#include "../ClientPredictionModelTypes.h"

#include "../../ClientPredictionNetSerialization.h"
#include "../../Input/AuthorityInputBuffer.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionAuthorityDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;

public:

	ClientPredictionAuthorityDriver(bool bTakesInput);

	// Simulation ticking

	virtual void Initialize() override;
	virtual void Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void BindToRepProxies(FClientPredictionRepProxy& NewAutoProxyRep, FClientPredictionRepProxy& NewSimProxyRep) override;

private:

	uint32 NextFrame = kInvalidFrame;

	/** Input packet used for the current frame */
	FInputPacketWrapper<InputPacket> CurrentInputPacket;

	WrappedState CurrentState;
	ModelState LastState;

	bool bAuthorityTakesInput = false;

	FAuthorityInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;

	FClientPredictionRepProxy* AutoProxyRep = nullptr;
	FClientPredictionRepProxy* SimProxyRep = nullptr;
};

template <typename InputPacket, typename ModelState, typename CueSet>
ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ClientPredictionAuthorityDriver(bool bAuthorityTakesInput) : bAuthorityTakesInput(bAuthorityTakesInput) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Initialize() {
	GenerateInitialState(CurrentState.State);
	LastState = CurrentState.State;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component) {
	if (!bAuthorityTakesInput && NextFrame == kInvalidFrame && InputBuffer.BufferSize() < kInputWindowSize) {
		return;
	}

	if (NextFrame == kInvalidFrame) {
		NextFrame = 0;
	}

	// Pre tick
	LastState = CurrentState.State;
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.RemainingAccumulatedTime = RemainingAccumulatedTime;
	CurrentState.Cues.Empty();

	if (!bAuthorityTakesInput) {
		uint8 FramesSpentInBuffer;
		const bool bBufferHadPacket = InputBuffer.ConsumeInput(CurrentInputPacket, FramesSpentInBuffer);

		if (!bBufferHadPacket) {
			UE_LOG(LogTemp, Verbose, TEXT("Dropped an input packet %d %d"), CurrentInputPacket.PacketNumber, InputBuffer.BufferSize());
		}

#ifdef CLIENT_PREDICTION_STATS
		CurrentState.AuthInputBufferSize        = InputBuffer.BufferSize() + static_cast<uint8>(bBufferHadPacket);
		CurrentState.AuthTimeSpentInInputBuffer = FramesSpentInBuffer;
#endif

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
			CurrentState.NetSerialize(Ar, true);
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
		InputBuffer.QueueInput(Packet);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::BindToRepProxies(FClientPredictionRepProxy& NewAutoProxyRep, FClientPredictionRepProxy& NewSimProxyRep) {
	AutoProxyRep = &NewAutoProxyRep;
	SimProxyRep = &NewSimProxyRep;

	AutoProxyRep->SerializeFunc = [&](FArchive& Ar) {
		CurrentState.NetSerialize(Ar, true);
		Ar << CurrentState.RemainingAccumulatedTime;
	};

	SimProxyRep->SerializeFunc = [&](FArchive& Ar) {
		CurrentState.NetSerialize(Ar, false);
	};
}