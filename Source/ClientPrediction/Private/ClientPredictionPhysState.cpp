#include "ClientPredictionPhysState.h"

namespace ClientPrediction {
    bool FPhysState::ShouldReconcile(const FPhysState& State) const {
        if (State.ObjectState != ObjectState) { return true; }
        if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
        if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
        if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
        if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

        return false;
    }

    void FPhysState::Interpolate(const FPhysState& Other, Chaos::FReal Alpha) {
        ObjectState = Other.ObjectState;
        X = FMath::Lerp(FVector(X), FVector(Other.X), Alpha);
        V = FMath::Lerp(FVector(V), FVector(Other.V), Alpha);
        R = FMath::Lerp(FQuat(R), FQuat(Other.R), Alpha);
        W = FMath::Lerp(FVector(W), FVector(Other.W), Alpha);
    }

    void FPhysState::Extrapolate(const FPhysState& PrevState, Chaos::FReal StateDt, Chaos::FReal ExtrapolationTime) {
        const Chaos::FVec3 Velocity = (X - PrevState.X) / StateDt;
        const Chaos::FVec3 AngularVelocity = Chaos::FRotation3::CalculateAngularVelocity(PrevState.R, R, StateDt);

        X += Velocity * ExtrapolationTime;
        R = Chaos::FRotation3::IntegrateRotationWithAngularVelocity(R, AngularVelocity, ExtrapolationTime);
    }

    void FPhysState::NetSerialize(FArchive& Ar, EDataCompleteness Completeness) {
        if (Completeness == EDataCompleteness::kLow) {
            SerializePackedVector<100, 30>(X, Ar);

            bool bOutSuccess = false;
            R.NetSerialize(Ar, nullptr, bOutSuccess);

            return;
        }

        Ar << ObjectState;

        // Serialize manually to make sure that they are serialized as doubles
        Ar << X.X;
        Ar << X.Y;
        Ar << X.Z;

        Ar << V.X;
        Ar << V.Y;
        Ar << V.Z;

        Ar << R.X;
        Ar << R.Y;
        Ar << R.Z;
        Ar << R.W;

        Ar << W.X;
        Ar << W.Y;
        Ar << W.Z;
    }
}
