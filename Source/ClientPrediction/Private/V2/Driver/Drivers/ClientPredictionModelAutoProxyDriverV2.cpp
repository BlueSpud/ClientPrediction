#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
FAutoConsoleVariableRef CVarClientPredictionFixedDt(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize, TEXT("The max size of the sliding window of inputs that is sent to the authority"));

namespace ClientPrediction {
	FModelAutoProxyDriver::FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent,
	                                             IModelDriverDelegate* Delegate,
	                                             FClientPredictionRepProxy& AutoProxyRep,
	                                             int32 RewindBufferSize) :
		UpdatedComponent(UpdatedComponent), Delegate(Delegate), RewindBufferSize(RewindBufferSize), InputBuf(RewindBufferSize) {
		check(UpdatedComponent);
		check(Delegate);

		BindToRepProxy(AutoProxyRep);
	}

	void FModelAutoProxyDriver::BindToRepProxy(FClientPredictionRepProxy& AutoProxyRep) {
		AutoProxyRep.SerializeFunc = [&](FArchive& Ar) {
			FPhysicsState AuthorityState;
			AuthorityState.NetSerialize(Ar);

			LastAuthorityState = AuthorityState;
		};
	}

	void FModelAutoProxyDriver::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
		// Sample a new input packet
		FInputPacketWrapper Packet;
		Packet.PacketNumber = TickNumber;

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

	int32 FModelAutoProxyDriver::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
		if (CurrentTickNumber <= LastAckedTick ) { return INDEX_NONE; }

		const FPhysicsState CurrentLastAuthorityState = LastAuthorityState;
		const int32 LocalTickNumber = CurrentLastAuthorityState.InputPacketTickNumber;
		if (LocalTickNumber == INDEX_NONE) { return INDEX_NONE; }

		// TODO Both of these cases should be handled gracefully
		check(LocalTickNumber <= CurrentTickNumber)
		check(LocalTickNumber >= CurrentTickNumber - RewindBufferSize)

		return INDEX_NONE;
	}

	void FModelAutoProxyDriver::PostPhysicsGameThread() {}
}
