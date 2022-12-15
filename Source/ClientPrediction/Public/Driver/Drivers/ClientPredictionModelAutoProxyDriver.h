#pragma once

#include <atomic>

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/Input/ClientPredictionAutoProxyInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
	extern CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize;

	template <typename StateType>
	class FModelAutoProxyDriver : public FSimulatedModelDriver<StateType>, public IRewindCallback  {
	public:
		FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<StateType>* InDelegate, FClientPredictionRepProxy& AutoProxyRep, int32 RewindBufferSize);
		virtual ~FModelAutoProxyDriver() override = default;

	private:
		void BindToRepProxy(FClientPredictionRepProxy& AutoProxyRep);

	public:

		// Ticking
		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		void ApplyCorrectionIfNeeded(int32 TickNumber);

		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		void UpdateHistory();

		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, const class Chaos::FRewindData& RewindData) override;
		virtual void PostPhysicsGameThread() override;

	private:
		FAutoProxyInputBuf InputBuf; // Written to on game thread, read from physics thread
		TArray<FPhysicsState<StateType>> History; // Only used on physics thread
		int32 RewindBufferSize = 0;

		FPhysicsState<StateType> PendingCorrection{}; // Only used on physics thread
		int32 PendingPhysicsCorrectionFrame = INDEX_NONE; // Only used on physics thread

		FCriticalSection LastAuthorityMutex;
		FPhysicsState<StateType> LastAuthorityState; // Written to from the game thread, read by the physics thread
		int32 LastAckedTick = INDEX_NONE; // Only used on the physics thread but might be used on the game thread later

		// Game thread
		TArray<FInputPacketWrapper> InputSlidingWindow;
	};

	template <typename StateType>
	FModelAutoProxyDriver<StateType>::FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent,
                                                 IModelDriverDelegate<StateType>* Delegate,
                                                 FClientPredictionRepProxy& AutoProxyRep,
                                                 int32 RewindBufferSize) :
    	FSimulatedModelDriver(UpdatedComponent, Delegate), InputBuf(RewindBufferSize), RewindBufferSize(RewindBufferSize) {
    	BindToRepProxy(AutoProxyRep);
    }

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::BindToRepProxy(FClientPredictionRepProxy& AutoProxyRep) {
    	AutoProxyRep.SerializeFunc = [&](FArchive& Ar) {
    		FScopeLock Lock(&LastAuthorityMutex);
    		LastAuthorityState.NetSerialize(Ar);
    	};
    }

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
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

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
    	ApplyCorrectionIfNeeded(TickNumber);

    	check(InputBuf.InputForTick(TickNumber, CurrentInput))
    	PreTickSimulateWithCurrentInput(TickNumber, Dt);
    }

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::ApplyCorrectionIfNeeded(int32 TickNumber) {
    	if (PendingPhysicsCorrectionFrame == INDEX_NONE) { return; }
    	check(PendingPhysicsCorrectionFrame == TickNumber);

    	auto* PhysicsHandle = GetPhysicsHandle();
    	PendingCorrection.Reconcile(PhysicsHandle);
    	LastState = MoveTemp(PendingCorrection);
    	PendingPhysicsCorrectionFrame = INDEX_NONE;
    }

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
    	PostTickSimulateWithCurrentInput(TickNumber, Dt, Time);
    	UpdateHistory();
    }

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::UpdateHistory() {
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

	template <typename StateType>
    int32 FModelAutoProxyDriver<StateType>::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
    	FScopeLock Lock(&LastAuthorityMutex);
    	const int32 LocalTickNumber = LastAuthorityState.InputPacketTickNumber;

    	if (LocalTickNumber == INDEX_NONE) { return INDEX_NONE; }
    	if (LocalTickNumber <= LastAckedTick ) { return INDEX_NONE; }

    	// TODO Both of these cases should be handled gracefully
    	check(LocalTickNumber <= CurrentTickNumber)
    	check(LocalTickNumber >= CurrentTickNumber - RewindBufferSize)

    	// Check against the historic state
    	const FPhysicsState<StateType>* HistoricState = nullptr;
    	for (const FPhysicsState<StateType>& State : History) {
    		if (State.TickNumber == LocalTickNumber) {
    			HistoricState = &State;
    			break;
    		}
    	}

    	check(HistoricState);
    	LastAckedTick = LocalTickNumber;

    	if (LastAuthorityState.ShouldReconcile(*HistoricState)) {

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

	template <typename StateType>
    void FModelAutoProxyDriver<StateType>::PostPhysicsGameThread() {}
}
