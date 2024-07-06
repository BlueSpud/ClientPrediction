#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionSimEvents.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FTickOutput {
        template <typename OtherStateType>
        friend struct FTickOutput;

        FTickOutput(StateType& State, const FNetTickInfo& TickInfo, const TSharedPtr<USimEvents>& SimEvents);
        StateType& State;

        template <typename OtherStateType>
        FTickOutput(const FTickOutput<OtherStateType>& Other);

        template <typename Event>
        void DispatchEvent(const Event& NewEvent);

    private:
        const FNetTickInfo& TickInfo;
        TSharedPtr<USimEvents> SimEvents;
    };

    template <typename StateType>
    FTickOutput<StateType>::FTickOutput(StateType& State, const FNetTickInfo& TickInfo, const TSharedPtr<USimEvents>& SimEvents) : State(State), TickInfo(TickInfo),
        SimEvents(SimEvents) {}

    template <typename StateType>
    template <typename OtherStateType>
    FTickOutput<StateType>::FTickOutput(const FTickOutput<OtherStateType>& Other) : State(Other.State), TickInfo(Other.TickInfo), SimEvents(Other.SimEvents) {
        static_assert(std::is_convertible_v<OtherStateType, StateType>);
    }

    template <typename Traits>
    template <typename Event>
    void FTickOutput<Traits>::DispatchEvent(const Event& NewEvent) {
        if (SimEvents == nullptr) { return; }
        SimEvents->template DispatchEvent<Event>(TickInfo, NewEvent);
    }

    template <typename Traits>
    struct FSimDelegates {
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;
        using TickOutput = FTickOutput<StateType>;

        FSimDelegates(const TSharedPtr<USimEvents>& SimEvents) : SimEvents(SimEvents) {}

        template <typename EventType>
        TMulticastDelegate<void(const EventType&, Chaos::FReal)>& RegisterEvent();

        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;

        DECLARE_MULTICAST_DELEGATE_OneParam(FInputGTDelegate, InputType& Input);
        FInputGTDelegate ProduceInputGTDelegate;

        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputPtDelegate, InputType& Input, const StateType& PrevState, const FSimTickInfo& TickInfo);
        FInputPtDelegate ModifyInputPTDelegate;

        DECLARE_MULTICAST_DELEGATE_FourParams(FSimTickDelegate, const FSimTickInfo& TickInfo, const InputType& Input, const StateType& PrevState, TickOutput& Output);
        FSimTickDelegate SimTickPrePhysicsDelegate;
        FSimTickDelegate SimTickPostPhysicsDelegate;

        DECLARE_MULTICAST_DELEGATE_TwoParams(FFinalizeDelegate, const StateType& State, Chaos::FReal Dt)
        FFinalizeDelegate FinalizeDelegate;

        DECLARE_MULTICAST_DELEGATE_FourParams(FExtrapolateDelegate, StateType&, const StateType&, Chaos::FReal, Chaos::FReal)
        FExtrapolateDelegate ExtrapolateDelegate;

        DECLARE_DELEGATE_RetVal_TwoParams(bool, FIsSimFinishedDelegate, const FSimTickInfo& TickInfo, const StateType& State)
        FIsSimFinishedDelegate IsSimFinishedDelegate;

    private:
        TSharedPtr<USimEvents> SimEvents;
    };

    template <typename Traits>
    template <typename EventType>
    TMulticastDelegate<void(const EventType&, Chaos::FReal)>& FSimDelegates<Traits>::RegisterEvent() {
        check(SimEvents != nullptr);
        return SimEvents->template RegisterEvent<EventType>();
    }
}
