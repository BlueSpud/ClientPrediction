#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 1;
	FAutoConsoleVariableRef CVarClientPredictionSlidingWindowSize(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize, TEXT("The max size of the sliding window of inputs that is sent to the authority"));

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
		Delegate->ProduceInput(Packet);

		InputBuf.QueueInputPacket(Packet);

		// Bundle it up with the most recent inputs and send it to the authority
		InputSlidingWindow.Add(Packet);
		while (InputSlidingWindow.Num() > ClientPredictionInputSlidingWindowSize) {
			InputSlidingWindow.RemoveAt(0);
		}

		check(InputSlidingWindow.Num() <= TNumericLimits<uint8>::Max())
		Delegate->EmitInputPackets(InputSlidingWindow);
	}

	void FModelAutoProxyDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		check(InputBuf.InputForTick(TickNumber, CurrentInputPacket))

		const auto Handle = UpdatedComponent->GetBodyInstance()->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		if (PendingCorrection.InputPacketTickNumber == TickNumber) {
			PendingCorrection.Reconcile(Handle);
			PendingCorrection = {};

			UE_LOG(LogTemp, Display, TEXT("Applied correction on %d"), TickNumber);
		}

		// Apply input
	}

	void FModelAutoProxyDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		// Generate state and marshall it to the game thread
	}

	int32 FModelAutoProxyDriver::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
		const FPhysicsState CurrentLastAuthorityState = LastAuthorityState;
		const int32 LocalTickNumber = CurrentLastAuthorityState.InputPacketTickNumber;

		if (LocalTickNumber == INDEX_NONE) { return INDEX_NONE; }
		if (LocalTickNumber <= LastAckedTick ) { return INDEX_NONE; }

		// TODO Both of these cases should be handled gracefully
		check(LocalTickNumber <= CurrentTickNumber)
		check(LocalTickNumber >= CurrentTickNumber - RewindBufferSize)

		// Check against the historic state
		const auto Handle = UpdatedComponent->GetBodyInstance()->GetPhysicsActorHandle()->GetHandle_LowLevel();
		const Chaos::FGeometryParticleState PreviousState = RewindData.GetPastStateAtFrame(*Handle, LocalTickNumber, Chaos::FFrameAndPhase::PostCallbacks);

		LastAckedTick = LocalTickNumber;
		if (CurrentLastAuthorityState.ShouldReconcile(PreviousState)) {
			PendingCorrection = LastAuthorityState;

			UE_LOG(LogTemp, Error, TEXT("Rewinding and rolling back to %d"), LocalTickNumber);
			return LocalTickNumber;
		}

		return INDEX_NONE;
	}

	void FModelAutoProxyDriver::PostPhysicsGameThread() {}
}
