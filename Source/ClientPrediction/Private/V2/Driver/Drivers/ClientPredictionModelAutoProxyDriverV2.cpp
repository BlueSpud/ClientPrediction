#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
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

	Chaos::FRigidBodyHandle_Internal* FModelAutoProxyDriver::GetPhysicsHandle() const {
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		check(BodyInstance)

		Chaos::FRigidBodyHandle_Internal* PhysicsHandle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		check(PhysicsHandle);

		return PhysicsHandle;
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
		auto* PhysicsHandle = GetPhysicsHandle();
		if (PendingPhysicsCorrectionFrame == TickNumber) {
			PendingCorrection.Reconcile(PhysicsHandle);
			CurrentState = MoveTemp(PendingCorrection);

			PendingCorrection = {};
			PendingPhysicsCorrectionFrame = INDEX_NONE;
		}

		check(InputBuf.InputForTick(TickNumber, CurrentInput))
		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = CurrentInput.PacketNumber;

		FPhysicsContext Context(PhysicsHandle);
		Delegate->SimulatePrePhysics(CurrentInput.Body.Get(), Context);
	}

	void FModelAutoProxyDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		auto* PhysicsHandle = GetPhysicsHandle();
		CurrentState.FillState(PhysicsHandle);

		const FPhysicsContext Context(PhysicsHandle);
		Delegate->SimulatePostPhysics(CurrentInput.Body.Get(), Context);

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
		const FPhysicsState CurrentLastAuthorityState = LastAuthorityState;
		const int32 LocalTickNumber = CurrentLastAuthorityState.InputPacketTickNumber;

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

		if (CurrentLastAuthorityState.ShouldReconcile(*HistoricState)) {

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
