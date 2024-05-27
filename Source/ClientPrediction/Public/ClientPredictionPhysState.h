#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionDataCompleteness.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API struct FPhysState {
        /** These mirror the Chaos properties for a particle */
        Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

        Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
        Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

        Chaos::FRotation3 R = Chaos::FRotation3::Identity;
        Chaos::FVec3 W = Chaos::FVec3::ZeroVector;

        CLIENTPREDICTION_API bool ShouldReconcile(const FPhysState& State) const;
        CLIENTPREDICTION_API void NetSerialize(FArchive& Ar, EDataCompleteness Completeness);
        CLIENTPREDICTION_API void Interpolate(const FPhysState& Other, Chaos::FReal Alpha);
        CLIENTPREDICTION_API void Extrapolate(const FPhysState& PrevState, Chaos::FReal StateDt, Chaos::FReal ExtrapolationTime);
    };
}
