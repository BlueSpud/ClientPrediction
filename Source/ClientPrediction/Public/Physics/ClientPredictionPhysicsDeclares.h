#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

struct FPhysicsState {
	
	Chaos::FVec3 Location = Chaos::FVec3::Zero();
	Chaos::FRotation3 Rotation = Chaos::FRotation3::Identity;
	Chaos::FVec3 LinearVelocity = Chaos::FVec3::Zero();
	Chaos::FVec3 AngularVelocity = Chaos::FVec3::Zero();

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

/**********************************************************************************************************************/

template <typename ModelState>
struct FPhysicsStateWrapper {
	
	ModelState State;
	FPhysicsState PhysicsState;

	void NetSerialize(FArchive& Ar);

	void Rewind(class UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle) const;
	void Interpolate(float Alpha, const FPhysicsStateWrapper& Other);

	bool operator ==(const FPhysicsStateWrapper<ModelState>& Other) const;
};

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::NetSerialize(FArchive& Ar) {
	State.NetSerialize(Ar);
	PhysicsState.NetSerialize(Ar);
}

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::Rewind(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle) const {
	PhysicsState.Rewind(Handle, Component);
	State.Rewind(Component);
}

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::Interpolate(float Alpha, const FPhysicsStateWrapper& Other) {
	PhysicsState.Interpolate(Alpha, Other.PhysicsState);
	State.Interpolate(Alpha, Other.State);
}

template <typename ModelState>
bool FPhysicsStateWrapper<ModelState>::operator==(const FPhysicsStateWrapper<ModelState>& Other) const {
	return State == Other.State
		&& PhysicsState == Other.PhysicsState;
}