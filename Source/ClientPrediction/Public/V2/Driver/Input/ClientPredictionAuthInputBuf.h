#pragma once

#include "ClientPredictionInput.h"

namespace ClientPrediction {
	struct FAuthInputBuf {

		void QueueInputPackets(const TArray<FInputPacketWrapper>& Packets);
		bool GetNextInputPacket(FInputPacketWrapper& OutPacket);
		uint32 GetBufferSize() const { return InputPackets.Num(); }

	private:
		void ConsumeFirstPacket(FInputPacketWrapper& OutPacket);

		TArray<FInputPacketWrapper> InputPackets;
		FInputPacketWrapper LastInputPacket{};

		FCriticalSection Mutex;
	};
}
