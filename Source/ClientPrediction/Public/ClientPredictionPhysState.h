#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionModelTypes.h"
#include "Data/ClientPredictionDataCompleteness.h"

namespace ClientPrediction {
    struct FPhysState {
        /** These mirror the Chaos properties for a particle */
        Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

        Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
        Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

        Chaos::FRotation3 R = Chaos::FRotation3::Identity;
        Chaos::FVec3 W = Chaos::FVec3::ZeroVector;

        bool ShouldReconcile(const FPhysState& State) const;
        void NetSerialize(FArchive& Ar, EDataCompleteness Completeness);
        void Interpolate(const FPhysState& Other, const Chaos::FReal Alpha);
    };

    inline bool FPhysState::ShouldReconcile(const FPhysState& State) const {
        if (State.ObjectState != ObjectState) { return true; }
        if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
        if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
        if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
        if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

        return false;
    }

    inline void FPhysState::Interpolate(const FPhysState& Other, const Chaos::FReal Alpha) {
        ObjectState = Other.ObjectState;
        X = FMath::Lerp(FVector(X), FVector(Other.X), Alpha);
        V = FMath::Lerp(FVector(V), FVector(Other.V), Alpha);
        R = FMath::Lerp(FQuat(R), FQuat(Other.R), Alpha);
        W = FMath::Lerp(FVector(W), FVector(Other.W), Alpha);
    }

    inline void FPhysState::NetSerialize(FArchive& Ar, EDataCompleteness Completeness) {
        if (Completeness == EDataCompleteness::kLow) {
            SerializeHalfPrecision(X, Ar);
            SerializeHalfPrecision(R, Ar);

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

    template <typename StateType>
    struct FWrappedState {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        StateType State{};
        FPhysState PhysState{};

        // These are not sent over the network, they're used for local interpolation only
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        void NetSerialize(FArchive& Ar, EDataCompleteness Completeness);
        void Interpolate(const FWrappedState& Other, Chaos::FReal Alpha);
    };
}
