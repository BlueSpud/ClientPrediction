#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "V2/World/ClientPredictionTickCallback.h"

namespace ClientPrediction {

	struct IPhysicsModelDelegate : public IModelDriverDelegate {
		virtual ~IPhysicsModelDelegate() override = default;
	};

	struct FPhysicsModel : public ITickCallback {
		FPhysicsModel() = default;

		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate);
		virtual ~FPhysicsModel() override;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);

		virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time) override;
		virtual void PostPhysicsGameThread() override;
		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber) override;

	private:
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};
}
