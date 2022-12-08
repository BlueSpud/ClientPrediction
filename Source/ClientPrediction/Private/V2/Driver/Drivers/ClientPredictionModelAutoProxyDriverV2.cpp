#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"

namespace ClientPrediction {
	void FModelAutoProxyDriver::Initialize(IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize) {
		Delegate = InDelegate;
		check(Delegate);
	}

	void FModelAutoProxyDriver::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAutoProxyDriver::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void FModelAutoProxyDriver::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) {

	}

	void FModelAutoProxyDriver::PostPhysicsGameThread() {

	}
}
