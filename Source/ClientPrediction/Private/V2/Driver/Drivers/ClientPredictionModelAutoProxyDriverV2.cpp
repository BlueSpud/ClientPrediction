#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
FAutoConsoleVariableRef CVarClientPredictionFixedDt(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize, TEXT("The max size of the sliding window of inputs that is sent to the authority"));

namespace ClientPrediction {
	FModelAutoProxyDriver::FModelAutoProxyDriver(IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize) : Delegate(InDelegate), InputBuf(RewindBufferSize) {
		check(InDelegate);
	}

	void FModelAutoProxyDriver::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
		// Sample a new input packet
		FInputPacketWrapper Packet;
		Packet.TickNumber = TickNumber;

		InputBuf.QueueInputPacket(Packet);

		// Bundle it up with the most recent inputs and send it to the authority
		InputSlidingWindow.Push(Packet);
		while (InputSlidingWindow.Num() >= ClientPredictionInputSlidingWindowSize) {
			InputSlidingWindow.Pop();
		}

		FNetSerializationProxy Proxy([=](FArchive& Ar) mutable {
			Ar << InputSlidingWindow;
		});

		Delegate->EmitInputPackets(Proxy);
	}

	void FModelAutoProxyDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		check(InputBuf.InputForTick(TickNumber, CurrentInputPacket));

		// Apply input
	}

	void FModelAutoProxyDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		// Generate state and marshall it to the game thread
	}

	void FModelAutoProxyDriver::PostPhysicsGameThread() {

	}
}
