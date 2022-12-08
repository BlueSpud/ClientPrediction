#include "V2/Physics/ClientPredictionPhysicsModelV2.h"

#include "V2/World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	void PhysicsModel::Initialize(UPrimitiveComponent* Component) {
		CachedWorld = Component->GetWorld();
		check(CachedWorld);

		FWorldManager* WorldManager = FWorldManager::ManagerForWorld(CachedWorld);
		check(WorldManager)

		WorldManager->AddTickCallback(this);
	}

	PhysicsModel::~PhysicsModel() {
		FWorldManager* WorldManager = FWorldManager::ManagerForWorld(CachedWorld);
		check(WorldManager)

		WorldManager->RemoveTickCallback(this);
	}

	void PhysicsModel::SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {

	}

	void PhysicsModel::PrepareTickGameThread(const int32 TickNumber, const Chaos::FReal Dt) {
		if (ModelDriver != nullptr) {
			ModelDriver->PrepareTickGameThread(TickNumber, Dt);
		}
	}

	void PhysicsModel::PreTickPhysicsThread(const int32 TickNumber, const Chaos::FReal Dt) {
		if (ModelDriver != nullptr) {
			ModelDriver->PreTickPhysicsThread(TickNumber, Dt);
		}
	}

	void PhysicsModel::PostTickPhysicsThread(const int32 TickNumber, const Chaos::FReal Dt, const Chaos::FReal Time) {
		if (ModelDriver != nullptr) {
			ModelDriver->PostTickPhysicsThread(TickNumber, Dt, Time);
		}
	}

	void PhysicsModel::PostPhysicsGameThread() {
		// Take simulation results and interpolates between them. The physics will already be interpolated by
		// Chaos, so this needs to be consistent with that. Unfortunately, the alpha is not exposed, so we need
		// to calculate it ourselves consistent with FChaosResultsChannel::PullAsyncPhysicsResults_External.
		// See also FSingleParticlePhysicsProxy::PullFromPhysicsState for how the physics state is interpolated from
		// a rewind state
		if (ModelDriver != nullptr) {
			ModelDriver->PostPhysicsGameThread();
		}
	}

	int32 PhysicsModel::GetRewindTickNumber(const int32 CurrentTickNumber) {
		if (ModelDriver != nullptr) {
			ModelDriver->GetRewindTickNumber(CurrentTickNumber);
		}

		return INDEX_NONE;
	}
}
