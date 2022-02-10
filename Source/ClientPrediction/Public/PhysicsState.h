#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

#include "PhysicsState.generated.h"

USTRUCT()
struct CLIENTPREDICTION_API FPhysicsState {

	GENERATED_BODY()
	
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;
	
	uint32 FrameNumber = 0;

	FPhysicsState() = default;
	explicit FPhysicsState(Chaos::FRigidBodyHandle_Internal* BodyHandle_Internal, uint32 FrameNumber) {
		check(BodyHandle_Internal);
		check(BodyHandle_Internal->CanTreatAsKinematic());
		
		Location = BodyHandle_Internal->X();
		Rotation = BodyHandle_Internal->R();
		LinearVelocity = BodyHandle_Internal->V();
		AngularVelocity = BodyHandle_Internal->W();
		
		this->FrameNumber = FrameNumber;
	}

	void Rewind(Chaos::FRigidBodyHandle_Internal* BodyHandle_Internal) const {
		check(BodyHandle_Internal);

		BodyHandle_Internal->SetX(Location);
		BodyHandle_Internal->SetR(Rotation);
		BodyHandle_Internal->SetV(LinearVelocity);
		BodyHandle_Internal->SetW(AngularVelocity);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
		Ar << Location;
		Ar << Rotation;
		Ar << LinearVelocity;
		Ar << AngularVelocity;
		Ar << FrameNumber;

		bOutSuccess = true;
		return true;
	}
	
};

template<>
struct TStructOpsTypeTraits<FPhysicsState> : public TStructOpsTypeTraitsBase2<FPhysicsState>
{
	enum
	{
		WithNetSerializer = true
	};
};