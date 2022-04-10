#pragma once

#include "Declares.h"

template <typename ModelState>
struct FModelStateWrapper {

	uint32 FrameNumber = kInvalidFrame;
	uint32 InputPacketNumber = kInvalidFrame;
	
	ModelState State;

	void NetSerialize(FArchive& Ar);

	bool operator ==(const FModelStateWrapper<ModelState>& Other) const;
};

template <typename ModelState>
void FModelStateWrapper<ModelState>::NetSerialize(FArchive& Ar)  {
	Ar << FrameNumber;
	Ar << InputPacketNumber;
		
	State.NetSerialize(Ar);
}

template <typename ModelState>
bool FModelStateWrapper<ModelState>::operator==(const FModelStateWrapper<ModelState>& Other) const {
	return InputPacketNumber == Other.InputPacketNumber
		&& State == Other.State;
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