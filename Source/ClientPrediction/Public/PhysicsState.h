#pragma once
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

USTRUCT()
struct CLIENTPREDICTION_API FPhysicsState {

	UPROPERTY()
	Chaos::FVec3 Location;

	UPROPERTY()
	Chaos::FRotation3 Rotation;

	UPROPERTY()
	Chaos::FVec3 LinearVelocity;

	UPROPERTY()
	Chaos::FVec3 AngularVelocity;

	UPROPERTY()
	uint32 FrameNumber = 0;

	explicit FPhysicsState(Chaos::FRigidBodyHandle_Internal* BodyHandle_Internal) {
		check(BodyHandle_Internal);
		check(BodyHandle_Internal->CanTreatAsKinematic());
		
		Location = BodyHandle_Internal->X();
		Rotation = BodyHandle_Internal->R();
		LinearVelocity = BodyHandle_Internal->V();
		AngularVelocity = BodyHandle_Internal->W();
	}

	void Rewind(Chaos::FRigidBodyHandle_Internal* BodyHandle_Internal) const {
		check(BodyHandle_Internal);

		BodyHandle_Internal->SetX(Location);
		BodyHandle_Internal->SetR(Rotation);
		BodyHandle_Internal->SetV(LinearVelocity);
		BodyHandle_Internal->SetW(AngularVelocity);
	}
	
};