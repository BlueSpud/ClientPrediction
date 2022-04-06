#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

struct FPhysicsState {
	
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;

	void Rewind(ImmediatePhysics::FActorHandle* Handle, UPrimitiveComponent* Component) const {
		FTransform RewindTransform;
		RewindTransform.SetLocation(Location);
		RewindTransform.SetRotation(Rotation);
		
		Handle->SetWorldTransform(RewindTransform);
		Handle->SetLinearVelocity(LinearVelocity);
		Handle->SetAngularVelocity(AngularVelocity);
	}

	void Interpolate(float Alpha, const FPhysicsState& Other) {
		Location = FMath::Lerp(Location, Other.Location, Alpha);
		Rotation = Chaos::FRotation3::FastLerp(Rotation, Other.Rotation, Alpha);
		LinearVelocity = FMath::Lerp(LinearVelocity, Other.LinearVelocity, Alpha);
		AngularVelocity = FMath::Lerp(AngularVelocity, Other.AngularVelocity, Alpha);
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