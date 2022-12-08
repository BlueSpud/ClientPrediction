#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	FModelAuthDriver::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* Delegate,
	                                   FClientPredictionRepProxy& AutoProxyRep,
	                                   FClientPredictionRepProxy& SimProxyRep) :
		UpdatedComponent(UpdatedComponent), Delegate(Delegate), AutoProxyRep(AutoProxyRep), SimProxyRep(SimProxyRep) {
		check(UpdatedComponent)
		check(Delegate)
	}

	void FModelAuthDriver::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAuthDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAuthDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {
		const auto* Handle = UpdatedComponent->GetBodyInstance()->ActorHandle->GetPhysicsThreadAPI();

		FPhysicsState CurrentState;
		CurrentState.TickNumber = TickNumber;
		CurrentState.InputPacketTickNumber = LastInput.PacketNumber;

		CurrentState.X = Handle->X();
		CurrentState.V = Handle->V();
		CurrentState.R = Handle->R();
		CurrentState.W = Handle->W();
		CurrentState.ObjectState = Handle->ObjectState();

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
}
