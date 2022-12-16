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
		FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize);
		virtual ~FModelAuthDriver() override = default;

	public:

		// Ticking
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;
		virtual void PostPhysicsGameThread(Chaos::FReal SimTime) override;

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
	FModelAuthDriver<InputType, StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize)
	: FSimulatedModelDriver(UpdatedComponent, Delegate, RewindBufferSize), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {}

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
	void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
		PostTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);

		FScopeLock Lock(&LastStateGtMutex);
		LastStateGt = CurrentState;
	}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime) {
		FPhysicsState<StateType> SendingState = LastStateGt;
		{
			FScopeLock Lock(&LastStateGtMutex);
			SendingState = LastStateGt;
		}

		if (SendingState.TickNumber != INDEX_NONE && SendingState.TickNumber != LastEmittedState) {

			AutoProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };
			SimProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };

			AutoProxyRep.Dispatch();
			SimProxyRep.Dispatch();

			LastEmittedState = SendingState.TickNumber;
		}
	}

	template <typename InputType, typename StateType>
	void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {
		InputBuf.QueueInputPackets(Packets);
	}
}
