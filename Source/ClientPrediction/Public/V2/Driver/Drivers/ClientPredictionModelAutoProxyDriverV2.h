#pragma once

#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "Driver/ClientPredictionRepProxy.h"

namespace ClientPrediction {
	class FModelAutoProxyDriver : public IModelDriver  {
	public:
		virtual void Initialize(IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep, int32 RewindBufferSize) override;
		virtual ~FModelAutoProxyDriver() override = default;

		// Ticking
		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;

	private:
		IModelDriverDelegate* Delegate = nullptr;
	};
}
