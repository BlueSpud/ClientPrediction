#pragma once

#include "Declares.h"

template <typename ModelState>
struct FModelStateWrapper {

	uint32 FrameNumber = kInvalidFrame;

	ModelState State;
	TArray<uint8> Cues;

	// These are only used for auth proxy
	Chaos::FReal RemainingAccumulatedTime;

	// Stats
#ifdef CLIENT_PREDICTION_STATS
	uint8 AuthInputBufferSize = 0;
	uint8 AuthTimeSpentInInputBuffer = 0;
#endif

	void NetSerialize(FArchive& Ar, bool bSerializeFullState);

	bool operator ==(const FModelStateWrapper<ModelState>& Other) const;
	void Print(FAnsiStringBuilderBase& Builder) const;
};

template <typename ModelState>
void FModelStateWrapper<ModelState>::NetSerialize(FArchive& Ar, bool bSerializeFullState)  {
	Ar << FrameNumber;
	Ar << Cues;

#ifdef CLIENT_PREDICTION_STATS
	if (bSerializeFullState) {
		Ar << AuthInputBufferSize;
		Ar << AuthTimeSpentInInputBuffer;
	}
#endif

	State.NetSerialize(Ar, bSerializeFullState);
}

template <typename ModelState>
bool FModelStateWrapper<ModelState>::operator==(const FModelStateWrapper<ModelState>& Other) const {
	if (Cues.Num() != Other.Cues.Num()) {
		return false;
	}

	for (int i = 0; i < Cues.Num(); i++) {
		if (Cues[i] != Other.Cues[i]) {
			return false;
		}
	}

	return State == Other.State;
}

template <typename ModelState>
void FModelStateWrapper<ModelState>::Print(FAnsiStringBuilderBase& Builder) const {
	Builder.Appendf("FrameNumber %d\n", FrameNumber);
	Builder.Appendf("Cues TODO\n");
	State.Print(Builder);
}

/**********************************************************************************************************************/

template <typename InputPacket>
struct FInputPacketWrapper {

	/** 
	 * Input frames have their own number independent of the frame number because they are not necessarily consumed in 
	 * lockstep with the frames they're generated on due to latency. 
	 */
	uint32 PacketNumber = kInvalidFrame;
	InputPacket Packet;

	void NetSerialize(FArchive& Ar);

	template <typename InputPacket_>
	friend FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket_>& Wrapper);

};

template <typename InputPacket>
void FInputPacketWrapper<InputPacket>::NetSerialize(FArchive& Ar) {
	Ar << PacketNumber;

	Packet.NetSerialize(Ar);
}

template <typename InputPacket>
FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket>& Wrapper) {
	Wrapper.NetSerialize(Ar);
	return Ar;
}

/**********************************************************************************************************************/

template <typename ModelState, typename CueSet>
struct FSimulationOutput {

	explicit FSimulationOutput(FModelStateWrapper<ModelState>& Wrapper);
	ModelState& State() const;
	void DispatchQueue(CueSet Cue) const;

private:

	FModelStateWrapper<ModelState>& Wrapper;
};

template <typename ModelState, typename CueSet>
FSimulationOutput<ModelState, CueSet>::FSimulationOutput(FModelStateWrapper<ModelState>& Wrapper) : Wrapper(Wrapper) {}

template <typename ModelState, typename CueSet>
ModelState& FSimulationOutput<ModelState, CueSet>::State() const {
	return Wrapper.State;
}

template <typename ModelState, typename CueSet>
void FSimulationOutput<ModelState, CueSet>::DispatchQueue(CueSet Cue) const {
	Wrapper.Cues.Add(static_cast<uint8>(Cue));
}
