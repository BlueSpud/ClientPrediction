#include "V2/Driver/Input/ClientPredictionAutoProxyInputBuf.h"

namespace ClientPrediction {
	FAutoProxyInputBuf::FAutoProxyInputBuf(int32 InputBufferMaxSize) :InputBufferMaxSize(InputBufferMaxSize) {
		InputPackets.Reserve(InputBufferMaxSize);
	}

	void FAutoProxyInputBuf::QueueInputPacket(const FInputPacketWrapper& Packet) {
		FScopeLock Lock(&Mutex);

		// Packets should come in sequentially on the auto proxy
		if (!InputPackets.IsEmpty()) {
			check(InputPackets.Last().TickNumber + 1 == Packet.TickNumber);

			if (InputPackets.Num() == InputBufferMaxSize) {
				InputPackets.RemoveAt(0);
			}
		}

		InputPackets.Add(Packet);
	}

	bool FAutoProxyInputBuf::InputForTick(const int32 TickNumber, FInputPacketWrapper& OutPacket) {
		FScopeLock Lock(&Mutex);

		for (const FInputPacketWrapper& Packet : InputPackets) {
			if (Packet.TickNumber == TickNumber) {
				OutPacket = Packet;
				return true;
			}
		}

		return false;
	}
}
