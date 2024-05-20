#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    template <typename Traits>
    struct FSimDelegates {
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;

        DECLARE_MULTICAST_DELEGATE_OneParam(FInputGTDelegate, InputType& Input);
        FInputGTDelegate ProduceInputGTDelegate; // Called on game thread

        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputPtDelegate, InputType& Input, Chaos::FReal Dt, int32 TickNumber);
        FInputPtDelegate ModifyInputPTDelegate;

        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;

        DECLARE_MULTICAST_DELEGATE_FourParams(FSimTickDelegate, const FSimTickInfo& TickInfo, const InputType& Input, const StateType& PrevState, StateType& OutState);
        FSimTickDelegate SimTickPrePhysicsDelegate;
        FSimTickDelegate SimTickPostPhysicsDelegate;

        DECLARE_MULTICAST_DELEGATE_TwoParams(FFinalizeDelegate, const StateType& State, Chaos::FReal Dt)
        FFinalizeDelegate FinalizeDelegate;
    };
}
