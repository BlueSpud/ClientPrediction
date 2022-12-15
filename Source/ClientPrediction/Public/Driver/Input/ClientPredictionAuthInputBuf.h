#pragma once

#include "ClientPredictionInput.h"

namespace ClientPrediction {
	struct CLIENTPREDICTION_API FAuthInputBuf {

		void QueueInputPackets(const TArray<FInputPacketWrapper>& Packets);
		bool GetNextInputPacket(FInputPacketWrapper& OutPacket);

		uint32 GetBufferSize() {
			FScopeLock Lock(&Mutex);
			return InputPackets.Num();
		}

	private:
		void ConsumeFirstPacket(FInputPacketWrapper& OutPacket);

		TArray<FInputPacketWrapper> InputPackets;
		FInputPacketWrapper LastInputPacket{};

		FCriticalSection Mutex;
	};
}
