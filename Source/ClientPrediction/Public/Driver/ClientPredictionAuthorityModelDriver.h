#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../AuthorityInputBuffer.h"

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

	uint8 CalculateInputBufferHealth();
	void SerializeCurrentStateForAutoProxy(FArchive& Ar);

private:

	uint32 NextFrame = kInvalidFrame;

	/** Input packet used for the current frame */
	FInputPacketWrapper<InputPacket> CurrentInputPacket;
	TArray<uint32> DroppedInputPackedIndices;

	WrappedState CurrentState;
	ModelState LastState;

	bool bAuthorityTakesInput = false;
	FAuthorityInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;

	FClientPredictionRepProxy* AutoProxyRep = nullptr;
	FClientPredictionRepProxy* SimProxyRep = nullptr;
};

// TODO fix
template <typename InputPacket, typename ModelState, typename CueSet>
ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::ClientPredictionAuthorityDriver(bool bAuthorityTakesInput) : bAuthorityTakesInput(false) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Initialize() {
	GenerateInitialState(CurrentState.State);
	LastState = CurrentState.State;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	if (!bAuthorityTakesInput && NextFrame == kInvalidFrame && InputBuffer.BufferSize() > 1) {
		return;
	}

	if (NextFrame == kInvalidFrame) {
		NextFrame = 0;
	}

	// Pre tick
	LastState = CurrentState.State;
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.Cues.Empty();

	if (!bAuthorityTakesInput) {

		uint8 FramesSpentInBuffer;
		if (!InputBuffer.ConsumeInput(CurrentInputPacket, FramesSpentInBuffer)) {
			DroppedInputPackedIndices.Push(CurrentState.FrameNumber);
			UE_LOG(LogTemp, Warning, TEXT("Dropped an input packet"));
		}

		CurrentState.NumRecentlyDroppedPackets = CalculateInputBufferHealth();
		CurrentState.FramesSpentInBuffer = FramesSpentInBuffer;
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
			SerializeCurrentStateForAutoProxy(Ar);
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
		SerializeCurrentStateForAutoProxy(Ar);
	};

	SimProxyRep->SerializeFunc = [&](FArchive& Ar) {
		CurrentState.NetSerialize(Ar);
	};
}

template <typename InputPacket, typename ModelState, typename CueSet>
uint8 ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::CalculateInputBufferHealth() {
	const uint32 CurrentFrameNumber = CurrentState.FrameNumber;

	// Drop any entries that are no longer needed
	if (CurrentFrameNumber > kBufferHealthTicks) {
		while (!DroppedInputPackedIndices.IsEmpty() && DroppedInputPackedIndices.Last() < CurrentFrameNumber - kBufferHealthTicks) {
			DroppedInputPackedIndices.Pop();
		}
	}

	return DroppedInputPackedIndices.Num();
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>::SerializeCurrentStateForAutoProxy(FArchive& Ar) {
	CurrentState.NetSerialize(Ar);
	Ar << CurrentState.NumRecentlyDroppedPackets;
	Ar << CurrentState.FramesSpentInBuffer;
}
