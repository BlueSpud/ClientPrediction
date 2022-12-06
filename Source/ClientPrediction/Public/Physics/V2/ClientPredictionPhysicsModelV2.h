#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "World/ClientPredictionTickCallback.h"

namespace ClientPrediction {

	struct PhysicsModel : public ITickCallback {
		PhysicsModel() = default;

		virtual void Initialize(class UPrimitiveComponent* Component);
		virtual ~PhysicsModel() override;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);

		virtual void PreTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, Chaos::FReal Dt) override;

	private:

		UWorld* CachedWorld = nullptr;
	};
}
