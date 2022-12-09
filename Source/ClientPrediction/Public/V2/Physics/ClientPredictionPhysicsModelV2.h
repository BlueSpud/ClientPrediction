#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"
#include "V2/World/ClientPredictionTickCallback.h"

namespace ClientPrediction {

	struct IPhysicsModelDelegate : public IModelDriverDelegate {
		virtual ~IPhysicsModelDelegate() override = default;
	};

	struct FPhysicsModel {
		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate);

		virtual ~FPhysicsModel() = default;
		void Cleanup();

		void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		void ReceiveInputPackets(FNetSerializationProxy& Proxy) const;

	private:
		class UPrimitiveComponent* CachedComponent = nullptr;
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};
}
