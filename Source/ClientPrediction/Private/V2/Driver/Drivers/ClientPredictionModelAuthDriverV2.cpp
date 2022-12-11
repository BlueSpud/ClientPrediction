#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize = 3;
	FAutoConsoleVariableRef CVarClientPredictionDesiredInputBufferSize(TEXT("cp.DesiredInputBufferSize"), ClientPredictionDesiredInputBufferSize, TEXT("The desired size of the input buffer on the authority"));

	FModelAuthDriver::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* Delegate,
	                                   FClientPredictionRepProxy& AutoProxyRep,
	                                   FClientPredictionRepProxy& SimProxyRep) :
		UpdatedComponent(UpdatedComponent), Delegate(Delegate), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {
		check(UpdatedComponent)
		check(Delegate)
	}

	void FModelAuthDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
		if (LastInput.PacketNumber == INDEX_NONE && InputBuf.GetBufferSize() < static_cast<uint32>(ClientPredictionDesiredInputBufferSize)) { return; }

		const bool bHadPacketInInputBuffer = InputBuf.GetNextInputPacket(LastInput);
		if (!bHadPacketInInputBuffer) {
			UE_LOG(LogTemp, Error, TEXT("Dropped an input packet %d on the authority"), LastInput.PacketNumber);
		}
	}

	void FModelAuthDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		const auto Handle = UpdatedComponent->GetBodyInstance()->GetPhysicsActorHandle()->GetPhysicsThreadAPI();

		FPhysicsState CurrentState;
		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = LastInput.PacketNumber;
		CurrentState.FillState(Handle);

		LastState = CurrentState;
	}

	void FModelAuthDriver::PostPhysicsGameThread() {
		FPhysicsState CurrentLastState = LastState;
		if (CurrentLastState.TickNumber != INDEX_NONE && CurrentLastState.TickNumber != LastEmittedState) {
			AutoProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { CurrentLastState.NetSerialize(Ar); };
			SimProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { CurrentLastState.NetSerialize(Ar); };

			AutoProxyRep.Dispatch();
			SimProxyRep.Dispatch();

			LastEmittedState = CurrentLastState.TickNumber;
		}
	}

	void FModelAuthDriver::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
		TArray<FInputPacketWrapper> Packets;
		Proxy.NetSerializeFunc = [&Packets](FArchive& Ar) {
			Ar << Packets;
		};

		Proxy.Deserialize();
		InputBuf.QueueInputPackets(Packets);
	}
}
