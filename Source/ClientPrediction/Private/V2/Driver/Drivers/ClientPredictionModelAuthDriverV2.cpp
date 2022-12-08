#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"

namespace ClientPrediction {
	void FModelAuthDriver::Initialize(IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize) {
		Delegate = InDelegate;
		check(Delegate);
	}

	void FModelAuthDriver::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAuthDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAuthDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {

	}

	void FModelAuthDriver::PostPhysicsGameThread() {

	}
}
