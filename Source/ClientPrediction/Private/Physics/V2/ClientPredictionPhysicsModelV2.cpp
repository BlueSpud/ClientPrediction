#include "Physics/V2/ClientPredictionPhysicsModelV2.h"

#include "World/ClientPredictionWorldManager.h"

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

	void PhysicsModel::PreTickGameThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void PhysicsModel::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	void PhysicsModel::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {

	}

	int32 PhysicsModel::GetRewindTickNumber(int32 CurrentTickNumber, Chaos::FReal Dt) {
		return INDEX_NONE;
	}
}
