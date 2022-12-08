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

	FPhysicsModel::~FPhysicsModel() {
		if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}
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
			ModelDriver = MakeUnique<FModelAutoProxyDriver>(CachedComponent, Delegate, AutoProxyRep, SimProxyRep, CachedWorldManager->GetRewindBufferSize());
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
}
