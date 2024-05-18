#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    template <typename Traits>
    struct FSimDelegates {
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;

        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputDelegate, InputType& Input, Chaos::FReal Dt, int32 TickNumber);
        FInputDelegate ProduceInputGTDelegate; // Called on game thread
        FInputDelegate ProduceInputPTDelegate; // Called on physics thread

        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;
    };
}
