#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize = 3;
	FAutoConsoleVariableRef CVarClientPredictionDesiredInputBufferSize(TEXT("cp.DesiredInputBufferSize"), ClientPredictionDesiredInputBufferSize, TEXT("The desired size of the input buffer on the authority"));

	FModelAuthDriver::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* Delegate,
	                                   FClientPredictionRepProxy& AutoProxyRep,
	                                   FClientPredictionRepProxy& SimProxyRep) :
		FSimulatedModelDriver(UpdatedComponent, Delegate), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {}

	void FModelAuthDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		if (CurrentInput.PacketNumber == INDEX_NONE && InputBuf.GetBufferSize() < static_cast<uint32>(ClientPredictionDesiredInputBufferSize)) { return; }

		const bool bHadPacketInInputBuffer = InputBuf.GetNextInputPacket(CurrentInput);
		if (!bHadPacketInInputBuffer) {
			UE_LOG(LogTemp, Error, TEXT("Dropped an input packet %d on the authority"), CurrentInput.PacketNumber);
		}

		PreTickSimulateWithCurrentInput(TickNumber, Dt);
	}

	void FModelAuthDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		PostTickSimulateWithCurrentInput(TickNumber, Dt, Time);

		FScopeLock Lock(&LastStateGtMutex);
		LastStateGt = CurrentState;
	}

	void FModelAuthDriver::PostPhysicsGameThread() {
		FScopeLock Lock(&LastStateGtMutex);

		if (LastStateGt.TickNumber != INDEX_NONE && LastStateGt.TickNumber != LastEmittedState) {

			FPhysicsState SendingState = LastStateGt;
			AutoProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { Delegate->NetSerialize(SendingState, Ar); };
			SimProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { Delegate->NetSerialize(SendingState, Ar); };

			AutoProxyRep.Dispatch();
			SimProxyRep.Dispatch();

			LastEmittedState = LastStateGt.TickNumber;
		}
	}

	void FModelAuthDriver::ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) {
		InputBuf.QueueInputPackets(Packets);
	}
}
