#pragma once

#include "ClientPredictionInput.h"

namespace ClientPrediction {

	template <typename InputType>
	struct FAuthInputBuf {
		using Wrapper = FInputPacketWrapper<InputType>;

		void QueueInputPackets(const TArray<Wrapper>& Packets);
		bool GetNextInputPacket(Wrapper& OutPacket);

		uint32 GetBufferSize() {
			FScopeLock Lock(&Mutex);
			return InputPackets.Num();
		}

	private:
		void ConsumeFirstPacket(Wrapper& OutPacket);

		TArray<Wrapper> InputPackets;
		Wrapper LastInputPacket{};

		FCriticalSection Mutex;
	};

	template <typename InputType>
	void FAuthInputBuf<InputType>::QueueInputPackets(const TArray<Wrapper>& Packets) {
		FScopeLock Lock(&Mutex);

		for (const Wrapper& Packet : Packets) {
			if (Packet.PacketNumber <= LastInputPacket.PacketNumber) { continue; }

			const bool bAlreadyHasPacket = InputPackets.ContainsByPredicate([&](const Wrapper& Candidate) {
				return Candidate.PacketNumber == Packet.PacketNumber;
			});

			if (!bAlreadyHasPacket) {
				InputPackets.Add(Packet);
			}
		}

		InputPackets.Sort([](const Wrapper& A, const Wrapper& B) {
			return A.PacketNumber < B.PacketNumber;
		});
	}

	template <typename InputType>
	bool FAuthInputBuf<InputType>::GetNextInputPacket(Wrapper& OutPacket) {
		FScopeLock Lock(&Mutex);

		if (LastInputPacket.PacketNumber == INDEX_NONE) {
			if (!InputPackets.IsEmpty()) {
				ConsumeFirstPacket(OutPacket);
				return true;
			}
		}

		const int32 ExpectedPacketNumber = LastInputPacket.PacketNumber + 1;
		if (!InputPackets.IsEmpty()) {
			if (InputPackets[0].PacketNumber == ExpectedPacketNumber) {
				ConsumeFirstPacket(OutPacket);
				return true;
			}
		}

		LastInputPacket.PacketNumber = ExpectedPacketNumber;
		OutPacket = LastInputPacket;

		return false;
	}

	template <typename InputType>
	void FAuthInputBuf<InputType>::ConsumeFirstPacket(Wrapper& OutPacket) {
		OutPacket = LastInputPacket = InputPackets[0];
		InputPackets.RemoveAt(0);
	}
}
