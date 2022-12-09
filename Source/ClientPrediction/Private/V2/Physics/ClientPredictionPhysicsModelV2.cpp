#include "V2/Physics/ClientPredictionPhysicsModelV2.h"

#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"
#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"
#include "V2/World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	void FPhysicsModel::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
		CachedComponent = Component;
		check(CachedComponent);

		Delegate = InDelegate;
		check(Delegate);

		const UWorld* World  = CachedComponent->GetWorld();
		check(World);

		CachedWorldManager = FWorldManager::ManagerForWorld(World);
		check(CachedWorldManager)
	}

	void FPhysicsModel::Cleanup() {
		if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		CachedWorldManager = nullptr;
		ModelDriver = nullptr;
	}

	void FPhysicsModel::SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
		check(CachedWorldManager);
		check(Delegate);

		if (ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		switch (Role) {
		case ROLE_Authority:
			// TODO pass bShouldTakeInput here
			ModelDriver = MakeUnique<FModelAuthDriver>(CachedComponent, Delegate, AutoProxyRep, SimProxyRep);
			break;
		case ROLE_AutonomousProxy:
			ModelDriver = MakeUnique<FModelAutoProxyDriver>(CachedComponent, Delegate, AutoProxyRep, CachedWorldManager->GetRewindBufferSize());
			break;
		case ROLE_SimulatedProxy:
			// TODO add in the sim proxy
			break;
		default:
			break;
		}

		if (ModelDriver != nullptr) {
			CachedWorldManager->AddTickCallback(ModelDriver.Get());
		}
	}

	void FPhysicsModel::ReceiveInputPackets(FNetSerializationProxy& Proxy) const {
		if (ModelDriver != nullptr) {
			ModelDriver->ReceiveInputPackets(Proxy);
		}
	}
}
