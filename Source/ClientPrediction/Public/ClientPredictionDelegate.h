#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionSimEvents.h"
#include "ClientPredictionTick.h"
#include "ClientPredictionPhysState.h"

namespace ClientPrediction {
    template <typename Traits>
    struct FTickOutput {
        using StateType = typename Traits::StateType;

        FTickOutput(StateType& State, const TSharedPtr<FSimEvents<Traits>>& SimEvents);
        typename Traits::StateType& State;

        template <typename Event>
        void DispatchEvent(const Event& NewEvent);

    private:
        TSharedPtr<FSimEvents<Traits>> SimEvents;
    };

    template <typename Traits>
    FTickOutput<Traits>::FTickOutput(StateType& State, const TSharedPtr<FSimEvents<Traits>>& SimEvents) : State(State), SimEvents(SimEvents) {}

    template <typename Traits>
    template <typename Event>
    void FTickOutput<Traits>::DispatchEvent(const Event& NewEvent) {
        if (SimEvents == nullptr) { return; }
    }


    template <typename Traits>
    struct FSimDelegates {
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;
        using TickOutput = FTickOutput<Traits>;

        DECLARE_MULTICAST_DELEGATE_OneParam(FInputGTDelegate, InputType& Input);
        FInputGTDelegate ProduceInputGTDelegate; // Called on game thread

        DECLARE_MULTICAST_DELEGATE_FourParams(FInputPtDelegate, InputType& Input, const StateType& PrevState, const FPhysState& PrevPhysState, Chaos::FReal Dt);
        FInputPtDelegate ModifyInputPTDelegate;

        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;

        DECLARE_MULTICAST_DELEGATE_FourParams(FSimTickDelegate, const FSimTickInfo& TickInfo, const InputType& Input, const StateType& PrevState, TickOutput& Output);
        FSimTickDelegate SimTickPrePhysicsDelegate;
        FSimTickDelegate SimTickPostPhysicsDelegate;

        DECLARE_MULTICAST_DELEGATE_TwoParams(FFinalizeDelegate, const StateType& State, Chaos::FReal Dt)
        FFinalizeDelegate FinalizeDelegate;
    };
}
