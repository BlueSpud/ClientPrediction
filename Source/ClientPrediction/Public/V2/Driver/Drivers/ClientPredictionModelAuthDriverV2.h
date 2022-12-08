#pragma once

#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelTypesV2.h"
#include "V2/Driver/Input/ClientPredictionInput.h"

namespace ClientPrediction {
	class FModelAuthDriver : public IModelDriver  {
	public:
		FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		virtual ~FModelAuthDriver() override = default;

		// Ticking
		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;

	private:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate* Delegate = nullptr;

		FClientPredictionRepProxy& AutoProxyRep;
		FClientPredictionRepProxy& SimProxyRep;

		FInputPacketWrapper LastInput{}; // Physics thread only

		std::atomic<FPhysicsState> LastState; // Written from physics thread, read on game thread
		int32 LastEmittedState = INDEX_NONE; // Game thread only
	};
}
