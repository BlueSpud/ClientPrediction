#pragma once

#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "V2/ClientPredictionModelTypesV2.h"
#include "V2/Driver/Input/ClientPredictionAuthInputBuf.h"
#include "V2/ClientPredictionInput.h"

namespace ClientPrediction {
	extern CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize;

	class CLIENTPREDICTION_API FModelAuthDriver : public IModelDriver  {
	public:
		FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		virtual ~FModelAuthDriver() override = default;

	private:
		class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

	public:

		// Ticking
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;

		// Called on game thread
		virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper>& Packets) override;

	private:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate* Delegate = nullptr;

		FClientPredictionRepProxy& AutoProxyRep;
		FClientPredictionRepProxy& SimProxyRep;

		FAuthInputBuf InputBuf; // Written to on game thread, read from physics thread
		FInputPacketWrapper CurrentInput{}; // Only used on physics thread
		FPhysicsState CurrentState{}; // Only used on physics thread
		FPhysicsState LastState{}; // Only used on physics thread

		FCriticalSection LastStateGtMutex;
		FPhysicsState LastStateGt; // Written from physics thread, read on game thread
		int32 LastEmittedState = INDEX_NONE; // Only used on game thread
	};
}
