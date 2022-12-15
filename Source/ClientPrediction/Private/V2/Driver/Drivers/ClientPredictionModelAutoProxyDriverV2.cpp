#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
	FAutoConsoleVariableRef CVarClientPredictionSlidingWindowSize(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize, TEXT("The max size of the sliding window of inputs that is sent to the authority"));

	FModelAutoProxyDriver::FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent,
	                                             IModelDriverDelegate* Delegate,
	                                             FClientPredictionRepProxy& AutoProxyRep,
	                                             int32 RewindBufferSize) :
		FSimulatedModelDriver(UpdatedComponent, Delegate), InputBuf(RewindBufferSize), RewindBufferSize(RewindBufferSize) {
		BindToRepProxy(AutoProxyRep);
	}

	void FModelAutoProxyDriver::BindToRepProxy(FClientPredictionRepProxy& AutoProxyRep) {
		AutoProxyRep.SerializeFunc = [&](FArchive& Ar) {
			Delegate->NewState(LastAuthorityState);
			Delegate->NetSerialize(LastAuthorityState, Ar);
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
		ApplyCorrectionIfNeeded(TickNumber);

		check(InputBuf.InputForTick(TickNumber, CurrentInput))
		PreTickSimulateWithCurrentInput(TickNumber, Dt);
	}

	void FModelAutoProxyDriver::ApplyCorrectionIfNeeded(int32 TickNumber) {
		if (PendingPhysicsCorrectionFrame == INDEX_NONE) { return; }
		check(PendingPhysicsCorrectionFrame == TickNumber);

		auto* PhysicsHandle = GetPhysicsHandle();
		PendingCorrection.Reconcile(PhysicsHandle);
		LastState = MoveTemp(PendingCorrection);
		PendingPhysicsCorrectionFrame = INDEX_NONE;
	}

	void FModelAutoProxyDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		PostTickSimulateWithCurrentInput(TickNumber, Dt, Time);
		UpdateHistory();
	}

	void FModelAutoProxyDriver::UpdateHistory() {
		if (History.IsEmpty() || History.Last().TickNumber < CurrentState.TickNumber) {
			History.Add(MoveTemp(CurrentState));
		} else {
			const int32 StartingTickNumber = History[0].TickNumber;
			History[CurrentState.TickNumber - StartingTickNumber] = MoveTemp(CurrentState);
		}

		// Trim the history buffer
		while (History.Num() > RewindBufferSize) {
			History.RemoveAt(0);
		}
	}

	int32 FModelAutoProxyDriver::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
		FScopeLock Lock(&LastAuthorityMutex);
		const int32 LocalTickNumber = LastAuthorityState.InputPacketTickNumber;

		if (LocalTickNumber == INDEX_NONE) { return INDEX_NONE; }
		if (LocalTickNumber <= LastAckedTick ) { return INDEX_NONE; }

		// TODO Both of these cases should be handled gracefully
		check(LocalTickNumber <= CurrentTickNumber)
		check(LocalTickNumber >= CurrentTickNumber - RewindBufferSize)

		// Check against the historic state
		const FPhysicsState* HistoricState = nullptr;
		for (const FPhysicsState& State : History) {
			if (State.TickNumber == LocalTickNumber) {
				HistoricState = &State;
				break;
			}
		}

		check(HistoricState);
		LastAckedTick = LocalTickNumber;

		if (Delegate->ShouldReconcile(LastAuthorityState, *HistoricState)) {

			// When we perform a correction, we add one to the frame, since LastAuthorityState will be the state
			// of the simulation during PostTickPhysicsThread (after physics has been simulated), so it is the beginning
			// state for LocalTickNumber + 1
			PendingPhysicsCorrectionFrame = LocalTickNumber + 1;
			PendingCorrection = LastAuthorityState;

			UE_LOG(LogTemp, Error, TEXT("Rewinding and rolling back to %d"), LocalTickNumber);
			return LocalTickNumber + 1;
		}

		return INDEX_NONE;
	}

	void FModelAutoProxyDriver::PostPhysicsGameThread() {}
}
