#pragma once

#include "V2/ClientPredictionInput.h"

namespace ClientPrediction {
		struct FAutoProxyInputBuf {
			FAutoProxyInputBuf(int32 InputBufferMaxSize);

			void QueueInputPacket(const FInputPacketWrapper& Packet);
			bool InputForTick(int32 TickNumber, FInputPacketWrapper& OutPacket);

		private:
			TArray<FInputPacketWrapper> InputPackets;
			int32 InputBufferMaxSize = 0;

			FCriticalSection Mutex;
		};
}
