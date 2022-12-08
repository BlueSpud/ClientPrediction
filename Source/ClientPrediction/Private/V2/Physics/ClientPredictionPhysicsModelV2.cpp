#include "V2/Physics/ClientPredictionPhysicsModelV2.h"

#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"
#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"
#include "V2/World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	void FPhysicsModel::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
		Delegate = InDelegate;
		check(Delegate);

		const UWorld* World  = Component->GetWorld();
		check(World);

		CachedWorldManager = FWorldManager::ManagerForWorld(World);
		check(CachedWorldManager)

		CachedWorldManager->AddTickCallback(this);
	}

	FPhysicsModel::~FPhysicsModel() {
		if (CachedWorldManager != nullptr) {
			CachedWorldManager->RemoveTickCallback(this);
		}
	}

	void FPhysicsModel::SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
		check(CachedWorldManager);
		check(Delegate);

		switch (Role) {
		case ROLE_Authority:
			// TODO pass bShouldTakeInput here
			ModelDriver = MakeUnique<FModelAuthDriver>();
			break;
		case ROLE_AutonomousProxy:
			ModelDriver = MakeUnique<FModelAutoProxyDriver>(Delegate, AutoProxyRep, SimProxyRep, CachedWorldManager->GetRewindBufferSize());
			break;
		case ROLE_SimulatedProxy:
			// TODO add in the sim proxy
			break;
		default:
			break;
		}
	}

	void FPhysicsModel::PrepareTickGameThread(const int32 TickNumber, const Chaos::FReal Dt) {
		if (ModelDriver != nullptr) {
			ModelDriver->PrepareTickGameThread(TickNumber, Dt);
		}
	}

	void FPhysicsModel::PreTickPhysicsThread(const int32 TickNumber, const Chaos::FReal Dt) {
		if (ModelDriver != nullptr) {
			ModelDriver->PreTickPhysicsThread(TickNumber, Dt);
		}
	}

	void FPhysicsModel::PostTickPhysicsThread(const int32 TickNumber, const Chaos::FReal Dt, const Chaos::FReal Time) {
		if (ModelDriver != nullptr) {
			ModelDriver->PostTickPhysicsThread(TickNumber, Dt, Time);
		}
	}

	void FPhysicsModel::PostPhysicsGameThread() {
		// Take simulation results and interpolates between them. The physics will already be interpolated by
		// Chaos, so this needs to be consistent with that. Unfortunately, the alpha is not exposed, so we need
		// to calculate it ourselves consistent with FChaosResultsChannel::PullAsyncPhysicsResults_External.
		// See also FSingleParticlePhysicsProxy::PullFromPhysicsState for how the physics state is interpolated from
		// a rewind state
		if (ModelDriver != nullptr) {
			ModelDriver->PostPhysicsGameThread();
		}
	}

	int32 FPhysicsModel::GetRewindTickNumber(const int32 CurrentTickNumber) {
		if (ModelDriver != nullptr) {
			ModelDriver->GetRewindTickNumber(CurrentTickNumber);
		}

		return INDEX_NONE;
	}
}
