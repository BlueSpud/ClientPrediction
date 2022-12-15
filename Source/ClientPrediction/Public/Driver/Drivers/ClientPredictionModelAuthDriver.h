#pragma once

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "ClientPredictionModelTypes.h"
#include "Driver/Input/ClientPredictionAuthInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/Input/ClientPredictionInput.h"

namespace ClientPrediction {
	extern CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize;

	template <typename InputType, typename StateType>
	class FModelAuthDriver final : public FSimulatedModelDriver<InputType, StateType>  {
	public:
		FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		virtual ~FModelAuthDriver() override = default;

	public:

		// Ticking
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;

		// Called on game thread
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) override;

	private:
		FClientPredictionRepProxy& AutoProxyRep;
		FClientPredictionRepProxy& SimProxyRep;
		FAuthInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread

		FCriticalSection LastStateGtMutex;
		FPhysicsState<StateType> LastStateGt; // Written from physics thread, read on game thread
		int32 LastEmittedState = INDEX_NONE; // Only used on game thread
	};

	template <typename InputType, typename StateType>
	FModelAuthDriver<InputType, StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent,
		IModelDriverDelegate<InputType, StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep,
		FClientPredictionRepProxy& SimProxyRep) : FSimulatedModelDriver(UpdatedComponent, Delegate), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		if (CurrentInput.PacketNumber == INDEX_NONE && InputBuf.GetBufferSize() < static_cast<uint32>(ClientPredictionDesiredInputBufferSize)) { return; }

		const bool bHadPacketInInputBuffer = InputBuf.GetNextInputPacket(CurrentInput);
		if (!bHadPacketInInputBuffer) {
			UE_LOG(LogTemp, Error, TEXT("Dropped an input packet %d on the authority"), CurrentInput.PacketNumber);
		}

		PreTickSimulateWithCurrentInput(TickNumber, Dt);
	}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		PostTickSimulateWithCurrentInput(TickNumber, Dt, Time);

		FScopeLock Lock(&LastStateGtMutex);
		LastStateGt = CurrentState;
	}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread() {
		FScopeLock Lock(&LastStateGtMutex);

		if (LastStateGt.TickNumber != INDEX_NONE && LastStateGt.TickNumber != LastEmittedState) {

			FPhysicsState SendingState = LastStateGt;
			AutoProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };
			SimProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };

			AutoProxyRep.Dispatch();
			SimProxyRep.Dispatch();

			LastEmittedState = LastStateGt.TickNumber;
		}
	}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {
		InputBuf.QueueInputPackets(Packets);
	}
}
