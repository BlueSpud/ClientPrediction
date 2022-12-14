#pragma once

#include <atomic>

#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "V2/Driver/Input/ClientPredictionAutoProxyInputBuf.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "V2/ClientPredictionInput.h"
#include "V2/ClientPredictionModelTypesV2.h"

namespace ClientPrediction {
	extern CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize;

	class CLIENTPREDICTION_API FModelAutoProxyDriver : public IModelDriver  {
	public:
		FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* InDelegate, FClientPredictionRepProxy& AutoProxyRep, int32 RewindBufferSize);
		virtual ~FModelAutoProxyDriver() override = default;

	private:
		void BindToRepProxy(FClientPredictionRepProxy& AutoProxyRep);
		class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

	public:

		// Ticking
		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;

		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		void UpdateHistory();

		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, const class Chaos::FRewindData& RewindData) override;
		virtual void PostPhysicsGameThread() override;

	private:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate* Delegate = nullptr;
		int32 RewindBufferSize = 0;

		FInputPacketWrapper CurrentInput{}; // Only used on physics thread
		FPhysicsState CurrentState{}; // Only used on physics thread
		FPhysicsState LastState{}; // Only used on physics thread

		FAutoProxyInputBuf InputBuf; // Written to on game thread, read from physics thread
		TArray<FPhysicsState> History; // Only used on physics thread

		FPhysicsState PendingCorrection{}; // Only used on physics thread
		int32 PendingPhysicsCorrectionFrame = INDEX_NONE; // Only used on physics thread

		FCriticalSection LastAuthorityMutex;
		FPhysicsState LastAuthorityState; // Written to from the game thread, read by the physics thread
		int32 LastAckedTick = INDEX_NONE; // Only used on the physics thread but might be used on the game thread later

		// Game thread
		TArray<FInputPacketWrapper> InputSlidingWindow;
	};
}
