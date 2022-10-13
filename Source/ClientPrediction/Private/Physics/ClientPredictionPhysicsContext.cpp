#include "Physics/ClientPredictionPhysicsContext.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

void FPhysicsContext::AddForce(const FVector& Force, bool bAccelerateChange) const {
	check(!isnan(Force.X))
	check(!isnan(Force.Y))
	check(!isnan(Force.Z))
	
	if (bAccelerateChange) {
		DynamicHandle->AddForce(Force * GetMass());
	} else {
		DynamicHandle->AddForce(Force);
	}

	SetBodySleeping(false);
}

void FPhysicsContext::AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange) const {
	check(!isnan(Torque.X))
	check(!isnan(Torque.Y))
	check(!isnan(Torque.Z))

	if (bAccelerateChange) {
		if (Chaos::FPBDRigidParticleHandle* Rigid = DynamicHandle->GetParticle()->CastToRigidParticle()) {
			DynamicHandle->AddTorque(Chaos::FParticleUtilitiesXR::GetWorldInertia(Rigid) * Torque);
		}
	} else {
		DynamicHandle->AddTorque(Torque);
	}

	SetBodySleeping(false);
}

void FPhysicsContext::AddImpulseAtLocation(FVector Impulse, FVector Location) const {
	check(!isnan(Impulse.X))
	check(!isnan(Impulse.Y))
	check(!isnan(Impulse.Z))

	check(!isnan(Location.X))
	check(!isnan(Location.Y))
	check(!isnan(Location.Z))
	
	DynamicHandle->AddImpulseAtLocation(Impulse, Location);
	SetBodySleeping(false);
}

Chaos::FReal FPhysicsContext::GetMass() const { return DynamicHandle->GetMass(); }
FVector FPhysicsContext::GetInertia() const { return DynamicHandle->GetInertia(); }
FVector FPhysicsContext::ScaleByMomentOfInertia(const FVector& InputVector) const {
	const FVector InputVectorLocal = GetTransform().InverseTransformVectorNoScale(InputVector);
	const FVector LocalScaled = InputVectorLocal * DynamicHandle->GetInertia();
	return GetTransform().TransformVectorNoScale(LocalScaled);
}

FTransform FPhysicsContext::GetTransform() const { return DynamicHandle->GetWorldTransform(); }
FTransform FPhysicsContext::GetPreviousTransform() const { return PreviousTransform; }
FVector FPhysicsContext::GetLinearVelocity() const { return DynamicHandle->GetLinearVelocity(); }
FVector FPhysicsContext::GetAngularVelocity() const { return DynamicHandle->GetAngularVelocity(); }

void FPhysicsContext::SetBodySleeping(const bool Sleeping) const {
	const auto RigidParticle = DynamicHandle->GetParticle()->CastToRigidParticle();

	// Check for pending forces
	if (Sleeping) {
		if (!RigidParticle->Acceleration().IsNearlyZero() ||
			!RigidParticle->AngularAcceleration().IsNearlyZero() ||
			!RigidParticle->AngularImpulseVelocity().IsNearlyZero() ||
			!RigidParticle->LinearImpulseVelocity().IsNearlyZero()) {
			return;
		}
	}

	if (RigidParticle != nullptr) {
		RigidParticle->SetSleeping(Sleeping);
	}
}

bool FPhysicsContext::LineTraceSingle(FHitResult& OutHit, const FVector& Start, const FVector& End) const {
	const UWorld* UnsafeWorld = Component->GetWorld();
	if (UnsafeWorld == nullptr) {
		return false;
	}

	FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
	Params.AddIgnoredActor(Component->GetOwner());

	return UnsafeWorld->LineTraceSingleByObjectType(OutHit, Start, End, FCollisionObjectQueryParams::AllStaticObjects, Params);
}

bool FPhysicsContext::SweepSingle(FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionShape& CollisionShape, const FQuat& Rotation) const {
	const UWorld* UnsafeWorld = Component->GetWorld();
	if (UnsafeWorld == nullptr) {
		return false;
	}

	FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
	Params.AddIgnoredActor(Component->GetOwner());

	return UnsafeWorld->SweepSingleByObjectType(OutHit, Start, End, Rotation, FCollisionObjectQueryParams::AllStaticObjects, CollisionShape, Params);
}
