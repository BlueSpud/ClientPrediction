#pragma once

namespace ClientPrediction {
    struct CLIENTPREDICTION_API FPhysicsContext {
        FPhysicsContext(class Chaos::FRigidBodyHandle_Internal* BodyHandle, class UPrimitiveComponent* Component)
            : BodyHandle(BodyHandle), Component(Component) {}

        void AddForce(const FVector& Force, bool bAccelerateChange = false);
        void AddTorqueInRadians(const FVector& Torque, bool bAccelerateChange = false);
        void AddImpulseAtLocation(FVector Impulse, FVector Location);
        void SetBodySleeping(const bool Sleeping);

        Chaos::FReal GetMass() const;
        FVector GetInertia() const;
        FVector ScaleByMomentOfInertia(const FVector& InputVector) const;

        FTransform GetTransform() const;
        FVector GetLinearVelocity() const;
        FVector GetAngularVelocity() const;

        // World query methods
        // TODO add more
        bool LineTraceSingle(struct FHitResult& OutHit, const FVector& Start, const FVector& End) const;
        // bool SweepSingle(struct FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionShape& CollisionShape, const FQuat& Rotation) const;

    private:
        class Chaos::FRigidBodyHandle_Internal* BodyHandle = nullptr;
        class UPrimitiveComponent* Component = nullptr;
    };
}
