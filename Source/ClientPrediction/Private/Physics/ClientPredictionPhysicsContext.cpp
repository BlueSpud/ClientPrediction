#include "Physics/ClientPredictionPhysicsContext.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

void FPhysicsContext::AddForce(const FVector& Force, bool bAccelerateChange) const {
	if (bAccelerateChange) {
		DynamicHandle->AddForce(Force * GetMass());
	} else {
		DynamicHandle->AddForce(Force);
	}
}
void FPhysicsContext::AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange) const {
	if (bAccelerateChange) {
		if (Chaos::FPBDRigidParticleHandle* Rigid = DynamicHandle->GetParticle()->CastToRigidParticle()) {
			DynamicHandle->AddTorque(Chaos::FParticleUtilitiesXR::GetWorldInertia(Rigid) * Torque);
		}
	} else {
		DynamicHandle->AddTorque(Torque);
	}
}
void FPhysicsContext::AddImpulseAtLocation(FVector Impulse, FVector Location) const { DynamicHandle->AddImpulseAtLocation(Impulse, Location); }

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

bool FPhysicsContext::LineTraceSingle(FHitResult& OutHit, const FVector& Start, const FVector& End) const {
	UWorld* UnsafeWorld = Component->GetWorld();
	if (UnsafeWorld == nullptr) {
		return false;
	}

	FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
	Params.AddIgnoredActor(Component->GetOwner());

	return UnsafeWorld->LineTraceSingleByObjectType(OutHit, Start, End, FCollisionObjectQueryParams::AllStaticObjects, Params);
}
