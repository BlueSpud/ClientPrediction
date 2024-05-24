#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionSimEvents.h"
#include "ClientPredictionTick.h"
#include "ClientPredictionPhysState.h"

namespace ClientPrediction {
    template <typename Traits>
    struct FTickOutput {
        using StateType = typename Traits::StateType;

        FTickOutput(StateType& State, const FNetTickInfo& TickInfo, const TSharedPtr<USimEvents<Traits>>& SimEvents);
        typename Traits::StateType& State;

        template <typename Event>
        void DispatchEvent(const Event& NewEvent);

    private:
        const FNetTickInfo& TickInfo;
        TSharedPtr<USimEvents<Traits>> SimEvents;
    };

    template <typename Traits>
    FTickOutput<Traits>::FTickOutput(StateType& State, const FNetTickInfo& TickInfo, const TSharedPtr<USimEvents<Traits>>& SimEvents) : State(State), TickInfo(TickInfo),
        SimEvents(SimEvents) {}

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
        using TickOutput = FTickOutput<Traits>;

        FSimDelegates(const TSharedPtr<USimEvents<Traits>>& SimEvents);

        template <typename EventType>
        TMulticastDelegate<void(const EventType&)>& RegisterEvent();

        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;

        DECLARE_MULTICAST_DELEGATE_OneParam(FInputGTDelegate, InputType& Input);
        FInputGTDelegate ProduceInputGTDelegate; // Called on game thread

        // TODO The phys state can be removed so long as this is called after corrections are applied
        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputPtDelegate, InputType& Input, const StateType& PrevState, Chaos::FReal Dt);
        FInputPtDelegate ModifyInputPTDelegate;

        DECLARE_MULTICAST_DELEGATE_FourParams(FSimTickDelegate, const FSimTickInfo& TickInfo, const InputType& Input, const StateType& PrevState, TickOutput& Output);
        FSimTickDelegate SimTickPrePhysicsDelegate;
        FSimTickDelegate SimTickPostPhysicsDelegate;

        DECLARE_MULTICAST_DELEGATE_TwoParams(FFinalizeDelegate, const StateType& State, Chaos::FReal Dt)
        FFinalizeDelegate FinalizeDelegate;

    private:
        TSharedPtr<USimEvents<Traits>> SimEvents;
    };

    template <typename Traits>
    FSimDelegates<Traits>::FSimDelegates(const TSharedPtr<USimEvents<Traits>>& SimEvents) : SimEvents(SimEvents) {}

    template <typename Traits>
    template <typename EventType>
    TMulticastDelegate<void(const EventType&)>& FSimDelegates<Traits>::RegisterEvent() {
        check(SimEvents != nullptr);
        return SimEvents->template RegisterEvent<EventType>();
    }
}
