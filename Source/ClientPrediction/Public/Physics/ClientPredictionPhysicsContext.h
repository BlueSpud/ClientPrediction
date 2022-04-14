#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"

/** 
 * Since a physics component operates in an immediate mode world, this struct provides the bridge and abstracts 
 * away certain things.
 */
struct CLIENTPREDICTION_API FPhysicsContext {

public:
	
	FPhysicsContext(ImmediatePhysics::FActorHandle* DynamicHandle, UPrimitiveComponent* Component)
		: DynamicHandle(DynamicHandle),
		  Component(Component) {}

// Dynamic actor methods
	void AddForce(const FVector& Force, bool bAccelerateChange = false) const;
	void AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange = false) const;
	void AddImpulseAtLocation(FVector Impulse, FVector Location) const;

	Chaos::FReal GetMass() const;
	FVector GetInertia() const;
	FVector ScaleByMomentOfInertia(const FVector& InputVector) const;

	FTransform GetTransform() const;
	FVector GetLinearVelocity() const;
	FVector GetAngularVelocity() const;

	
	// World query methods
	// TODO add more
	bool LineTraceSingle(struct FHitResult& OutHit, const FVector& Start, const FVector& End) const;
	
private:
    
    /** The handle to the dynamic actor in the simulation. */
    ImmediatePhysics::FActorHandle* DynamicHandle;

	/** The component that the immediate mode simulation is simulating dynamically. */
	class UPrimitiveComponent* Component;
	
};