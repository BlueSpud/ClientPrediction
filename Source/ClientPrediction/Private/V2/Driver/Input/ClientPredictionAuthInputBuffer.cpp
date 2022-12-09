#include "V2/Driver/Input/ClientPredictionAuthInputBuf.h"

namespace ClientPrediction {
	void FAuthInputBuf::QueueInputPackets(const TArray<FInputPacketWrapper>& Packets) {
		FScopeLock Lock(&Mutex);

		for (const FInputPacketWrapper& Packet : Packets) {
			if (Packet.PacketNumber <= LastInputPacket.PacketNumber) { continue; }

			const bool bAlreadyHasPacket = InputPackets.ContainsByPredicate([&](const FInputPacketWrapper& Candidate) {
				return Candidate.PacketNumber == Packet.PacketNumber;
			});

			if (!bAlreadyHasPacket) {
				InputPackets.Add(Packet);
			}
		}

		InputPackets.Sort([](const FInputPacketWrapper& A, const FInputPacketWrapper& B) {
			return A.PacketNumber < B.PacketNumber;
		});
	}

	bool FAuthInputBuf::GetNextInputPacket(FInputPacketWrapper& OutPacket) {
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

	void FAuthInputBuf::ConsumeFirstPacket(FInputPacketWrapper& OutPacket) {
		OutPacket = LastInputPacket = InputPackets[0];
		InputPackets.RemoveAt(0);
	}
}
