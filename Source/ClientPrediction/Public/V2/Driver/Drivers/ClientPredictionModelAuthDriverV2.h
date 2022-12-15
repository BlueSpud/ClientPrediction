#pragma once

#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "V2/ClientPredictionModelTypesV2.h"
#include "V2/Driver/Input/ClientPredictionAuthInputBuf.h"
#include "V2/Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "V2/ClientPredictionInput.h"

namespace ClientPrediction {
	extern CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize;

	template <typename StateType>
	class FModelAuthDriver : public FSimulatedModelDriver<StateType>  {
	public:
		FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		virtual ~FModelAuthDriver() override = default;

	public:

		// Ticking
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;

		// Called on game thread
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) override;

	private:
		FClientPredictionRepProxy& AutoProxyRep;
		FClientPredictionRepProxy& SimProxyRep;
		FAuthInputBuf InputBuf; // Written to on game thread, read from physics thread

		FCriticalSection LastStateGtMutex;
		FPhysicsState<StateType> LastStateGt; // Written from physics thread, read on game thread
		int32 LastEmittedState = INDEX_NONE; // Only used on game thread
	};

	template <typename StateType>
	FModelAuthDriver<StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent,
		IModelDriverDelegate<StateType>* Delegate, FClientPredictionRepProxy& AutoProxyRep,
		FClientPredictionRepProxy& SimProxyRep) : FSimulatedModelDriver(UpdatedComponent, Delegate), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {}

	template <typename StateType>
	void FModelAuthDriver<StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		if (CurrentInput.PacketNumber == INDEX_NONE && InputBuf.GetBufferSize() < static_cast<uint32>(ClientPredictionDesiredInputBufferSize)) { return; }

		const bool bHadPacketInInputBuffer = InputBuf.GetNextInputPacket(CurrentInput);
		if (!bHadPacketInInputBuffer) {
			UE_LOG(LogTemp, Error, TEXT("Dropped an input packet %d on the authority"), CurrentInput.PacketNumber);
		}

		PreTickSimulateWithCurrentInput(TickNumber, Dt);
	}

	template <typename StateType>
	void FModelAuthDriver<StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		PostTickSimulateWithCurrentInput(TickNumber, Dt, Time);

		FScopeLock Lock(&LastStateGtMutex);
		LastStateGt = CurrentState;
	}

	template <typename StateType>
	void FModelAuthDriver<StateType>::PostPhysicsGameThread() {
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

	template <typename StateType>
	void FModelAuthDriver<StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) {
		InputBuf.QueueInputPackets(Packets);
	}
}
