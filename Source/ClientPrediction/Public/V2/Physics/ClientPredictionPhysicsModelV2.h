#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "V2/World/ClientPredictionTickCallback.h"

namespace ClientPrediction {

	struct PhysicsModel : public ITickCallback {
		PhysicsModel() = default;

		virtual void Initialize(class UPrimitiveComponent* Component);
		virtual ~PhysicsModel() override;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);

		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;
		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber) override;

	private:
		UWorld* CachedWorld = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
	};
}
