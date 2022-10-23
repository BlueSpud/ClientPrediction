#pragma once

#include "Declares.h"

template <typename InputPacket>
class FAuthorityInputBuffer {

private:

	struct FInputBufferPacketWrapper {
		InputPacket Packet;
		uint8 FramesSpentInBuffer = 0;
	};

public:

	uint32 BufferSize() {
		return PacketBuffer.Num();
	}

	void QueueInput(const InputPacket& Packet) {

		// Ensure that the packet is not in the past
		if (InputPacketNumber != kInvalidFrame && Packet.PacketNumber <= InputPacketNumber) {
			return;
		}

		// Ensure that the packet is not already queued
		const FInputBufferPacketWrapper* ExistingPacket = PacketBuffer.FindByPredicate([&](const FInputBufferPacketWrapper& Candidate) {
			return Candidate.Packet.PacketNumber == Packet.PacketNumber;
		});

		if (ExistingPacket != nullptr) {
			return;
		}

		FInputBufferPacketWrapper NewPacket;
		NewPacket.Packet = Packet;
		NewPacket.FramesSpentInBuffer = 0;

		PacketBuffer.Add(NewPacket);

		// Sort the input buffer in case packets have come in out of order
		//
		// Pop() on a TArray takes the last element, so we want the input buffer to be sorted from greatest to least.
		// Then when we take the last element that will be the lowest number (ie next) input packet.
		PacketBuffer.Sort([](const FInputBufferPacketWrapper& First, const FInputBufferPacketWrapper& Second) {
			return First.Packet.PacketNumber > Second.Packet.PacketNumber;
		});
	}

	/** Returns true if there was a packet in the buffer to get */
	bool ConsumeInput(InputPacket& OutPacket, uint8& FramesSpentInBuffer) {
		bool bFoundInputPacket = false;

		InputPacketNumber = InputPacketNumber == kInvalidFrame ? 0 : InputPacketNumber + 1;
		if (!PacketBuffer.IsEmpty() && PacketBuffer.Last().Packet.PacketNumber == InputPacketNumber) {
			bFoundInputPacket = true;

			LastPacket = PacketBuffer.Pop();
			OutPacket = LastPacket.Packet;
			FramesSpentInBuffer = LastPacket.FramesSpentInBuffer;
		} else {
			OutPacket = LastPacket.Packet;
			FramesSpentInBuffer = 0;
		}

		for (FInputBufferPacketWrapper& Packet : PacketBuffer) {
			++Packet.FramesSpentInBuffer;
		}

		return bFoundInputPacket;
	}


private:

	/** The inputs */
	TArray<FInputBufferPacketWrapper> PacketBuffer;

	/** The frame number of the last input packet consumed */
	uint32 InputPacketNumber = kInvalidFrame;

	/** The last packet consumed */
	FInputBufferPacketWrapper LastPacket;

};