#include "Physics/ClientPredictionPhysicsContext.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
	void FPhysicsContext::AddForce(const FVector& Force, bool bAccelerateChange) {
		if (bAccelerateChange) { BodyHandle->AddForce(Force * GetMass()); }
		else { BodyHandle->AddForce(Force); }
	}

	void FPhysicsContext::AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange) {
		if (bAccelerateChange) {
			BodyHandle->AddTorque(Chaos::FParticleUtilitiesXR::GetWorldInertia(BodyHandle) * Torque);
		}
		else { BodyHandle->AddTorque(Torque); }
	}

	void FPhysicsContext::AddImpulseAtLocation(FVector Impulse, FVector Location) {
		const Chaos::FVec3 WorldCOM = Chaos::FParticleUtilitiesXR::GetCoMWorldPosition(BodyHandle);
		BodyHandle->SetLinearImpulse(BodyHandle->LinearImpulse() + Impulse, false);
		AddAngularImpulse(Chaos::FVec3::CrossProduct(Location - WorldCOM, Impulse));
	}

	void FPhysicsContext::AddAngularImpulse(FVector AngularImpulse) {
		BodyHandle->SetAngularImpulse(BodyHandle->AngularImpulse() + AngularImpulse, false);
	}

	void FPhysicsContext::SetBodySleeping(const bool Sleeping) {
		if (Sleeping) {
			if (!BodyHandle->Acceleration().IsNearlyZero() ||
				!BodyHandle->AngularAcceleration().IsNearlyZero() ||
				!BodyHandle->AngularImpulseVelocity().IsNearlyZero() ||
				!BodyHandle->LinearImpulseVelocity().IsNearlyZero()) {
				return;
			}
		}

		const Chaos::EObjectStateType DesiredState = Sleeping ? Chaos::EObjectStateType::Sleeping : Chaos::EObjectStateType::Dynamic;
		if (BodyHandle->ObjectState() == DesiredState) {
			return;
		}

		BodyHandle->SetObjectState(DesiredState);
	}

	Chaos::FReal FPhysicsContext::GetMass() const { return BodyHandle->M(); }
	FVector FPhysicsContext::GetInertia() const { return FVector(BodyHandle->I()); }

	FVector FPhysicsContext::ScaleByMomentOfInertia(const FVector& InputVector) const {
		const FVector InputVectorLocal = GetTransform().InverseTransformVectorNoScale(InputVector);
		const FVector LocalScaled = InputVectorLocal * GetInertia();
		return GetTransform().TransformVectorNoScale(LocalScaled);
	}

	FTransform FPhysicsContext::GetTransform() const { return {BodyHandle->R(), BodyHandle->X(), FVector(1.0)}; }
	const FTransform& FPhysicsContext::GetPreviousTransform() const { return PrevTransform; }
	FVector FPhysicsContext::GetCenterOfMass() const { return Chaos::FParticleUtilitiesXR::GetCoMWorldPosition(BodyHandle); }

	FVector FPhysicsContext::GetLinearVelocity() const { return BodyHandle->V(); }
	FVector FPhysicsContext::GetLinearVelocityAtLocation(const FVector& WorldLocation) const {
		return FPhysicsInterface::GetWorldVelocityAtPoint_AssumesLocked(BodyHandle, WorldLocation);
	}

	FVector FPhysicsContext::GetAngularVelocity() const { return BodyHandle->W(); }

	bool FPhysicsContext::LineTraceSingle(FHitResult& OutHit, const FVector& Start, const FVector& End) const {
		const UWorld* World = Component->GetWorld();
		if (World == nullptr) { return false; }

		FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
		QueryParams.AddIgnoredActor(Component->GetOwner());
		QueryParams.bTraceComplex = false;

		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_WorldStatic, QueryParams);
	}

	// bool FPhysicsContext::SweepSingle(FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionShape& CollisionShape, const FQuat& Rotation) const {
	//
	// 	return false;
	// }
}
