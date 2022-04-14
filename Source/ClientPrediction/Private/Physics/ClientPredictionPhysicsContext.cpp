#include "Physics/ClientPredictionPhysicsContext.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

void FPhysicsContext::AddForce(const FVector& Force) const { DynamicHandle->AddForce(Force); }
void FPhysicsContext::AddTorqueInRadians(const FVector& Torque) const {

	// The immediate mode simulation does torque in degrees, not radians so it needs to be converted
	DynamicHandle->AddTorque(FMath::DegreesToRadians(Torque));
}
void FPhysicsContext::AddImpulseAtLocation(FVector Impulse, FVector Location) const { DynamicHandle->AddImpulseAtLocation(Impulse, Location); }

Chaos::FReal FPhysicsContext::GetMass() const { return DynamicHandle->GetMass(); }
FVector FPhysicsContext::GetInertia() const { return DynamicHandle->GetInertia(); }

FTransform FPhysicsContext::GetTransform() const { return DynamicHandle->GetWorldTransform(); }
FVector FPhysicsContext::GetLinearVelocity() const { return DynamicHandle->GetLinearVelocity(); }
FVector FPhysicsContext::GetAngularVelocity() const { return DynamicHandle->GetLinearVelocity(); }

bool FPhysicsContext::LineTraceSingle(FHitResult& OutHit, const FVector& Start, const FVector& End) const {
	UWorld* UnsafeWorld = Component->GetWorld();
	if (UnsafeWorld == nullptr) {
		return false;
	}

	FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
	Params.AddIgnoredActor(Component->GetOwner());
	
	return UnsafeWorld->LineTraceSingleByObjectType(OutHit, Start, End, FCollisionObjectQueryParams::AllStaticObjects, Params);
}
