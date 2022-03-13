#pragma once

#include "ClientPredictionModel.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

struct FPhysicsState {
	
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;

	void Rewind(UPrimitiveComponent* Component) const {
		if (Component->IsSimulatingPhysics()) {
			FBodyInstance* Body = Component->GetBodyInstance();
			Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
			
			Handle->SetX(Location);
			Handle->SetR(Rotation);
			Handle->SetV(LinearVelocity);
			Handle->SetW(AngularVelocity);
		} else {
			Component->SetWorldLocation(Location);
			Component->SetWorldRotation(Rotation);
		}
	}

	void NetSerialize(FArchive& Ar) {
		Ar << Location;
		Ar << Rotation;
		Ar << LinearVelocity;
		Ar << AngularVelocity;
	}
	
	bool operator ==(const FPhysicsState& Other) const {
		return Location.Equals(Other.Location, 0.2)
			&& Rotation.Equals(Other.Rotation, 0.2)
			&& LinearVelocity.Equals(Other.LinearVelocity, 0.2)
			&& AngularVelocity.Equals(Other.AngularVelocity, 0.2);
	}
	
};