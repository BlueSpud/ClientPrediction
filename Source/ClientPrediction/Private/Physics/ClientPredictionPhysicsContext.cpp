#include "Physics/ClientPredictionPhysicsContext.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	void FPhysicsContext::AddForce(const FVector& Force, bool bAccelerateChange) {
		SetBodySleeping(false);

		if (bAccelerateChange) { BodyHandle->AddForce(Force * GetMass()); }
		else { BodyHandle->AddForce(Force); }
	}

	void FPhysicsContext::AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange) {
		SetBodySleeping(false);

		if (bAccelerateChange) { BodyHandle->AddTorque(Chaos::FParticleUtilitiesXR::GetWorldInertia(BodyHandle) * Torque);
		} else { BodyHandle->AddTorque(Torque); }
	}

	void FPhysicsContext::AddImpulseAtLocation(FVector Impulse, FVector Location) {
		const Chaos::FVec3 WorldCOM = Chaos::FParticleUtilitiesXR::GetCoMWorldPosition(BodyHandle);
		BodyHandle->SetLinearImpulse(BodyHandle->LinearImpulseVelocity() + (Impulse * BodyHandle->InvM()), true, false);

		const Chaos::FMatrix33 WorldInvI = Chaos::Utilities::ComputeWorldSpaceInertia(BodyHandle->R() * BodyHandle->RotationOfMass(), BodyHandle->InvI());
		const Chaos::FVec3 AngularImpulse = Chaos::FVec3::CrossProduct(Location - WorldCOM, Impulse);
		BodyHandle->SetAngularImpulse(BodyHandle->AngularImpulseVelocity() + WorldInvI * AngularImpulse, true, false);

		SetBodySleeping(false);
	}

	void FPhysicsContext::SetBodySleeping(const bool Sleeping) {
		// Check for pending forces
		if (Sleeping) {
			if (!BodyHandle->Acceleration().IsNearlyZero() ||
				!BodyHandle->AngularAcceleration().IsNearlyZero() ||
				!BodyHandle->AngularImpulseVelocity().IsNearlyZero() ||
				!BodyHandle->LinearImpulseVelocity().IsNearlyZero()) {
				return;
			}
		}

		BodyHandle->SetObjectState(Sleeping ? Chaos::EObjectStateType::Sleeping : Chaos::EObjectStateType::Dynamic);
	}

	Chaos::FReal FPhysicsContext::GetMass() const { return BodyHandle->M(); }
	FVector FPhysicsContext::GetInertia() const { return FVector(BodyHandle->I()); }

	FVector FPhysicsContext::ScaleByMomentOfInertia(const FVector& InputVector) const {
		const FVector InputVectorLocal = GetTransform().InverseTransformVectorNoScale(InputVector);
		const FVector LocalScaled = InputVectorLocal * GetInertia();
		return GetTransform().TransformVectorNoScale(LocalScaled);
	}

	FTransform FPhysicsContext::GetTransform() const { return { BodyHandle->R(), BodyHandle->X(), FVector::ZeroVector}; }
	FVector FPhysicsContext::GetLinearVelocity() const { return BodyHandle->V(); }
	FVector FPhysicsContext::GetAngularVelocity() const { return BodyHandle->W(); }

	bool FPhysicsContext::LineTraceSingle(FHitResult& OutHit, const FVector& Start, const FVector& End) const {
		const UWorld* World = Component->GetWorld();
		if (World == nullptr) { return false; }

		FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
		QueryParams.AddIgnoredActor(Component->GetOwner());

		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_WorldStatic, QueryParams);
	}

	// bool FPhysicsContext::SweepSingle(FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionShape& CollisionShape, const FQuat& Rotation) const {
	//
	// 	return false;
	// }
}
