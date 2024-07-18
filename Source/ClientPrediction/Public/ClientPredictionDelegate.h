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

        /** The output state of the simulation tick. */
        StateType& State;

        template <typename OtherStateType>
        FTickOutput(const FTickOutput<OtherStateType>& Other);

        /**
         * Dispatches an event. There will be a delay between when this function is called and when the delegate for the event is broadcasted since some physics ticks
         * are buffered before the game thread interpolates between them. Events will be replicated reliably, so usage should be kept to a minimum.
         *
         * Events need to be registered first before they can be dispatched.
         * @tparam Event The event type to dispatch.
         * @param NewEvent The event data.
         */
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

        /**
         * Registers an event for the simulation. The event can be dispatched in SimTickPrePhysicsDelegate or SimTickPostPhysicsDelegate.
         * @tparam EventType The event type to register.
         * @return The delegate that will be called on the game thread when the event is dispatched.
         */
        template <typename EventType>
        TMulticastDelegate<void(const EventType&, Chaos::FReal)>& RegisterEvent();

        /**
         * Generates the initial state for the simulation. This is broadcasted on the physics thread on the authority and auto proxy on the first tick of the simulation.
         * @param [out] State The generated initial state.
         */
        DECLARE_MULTICAST_DELEGATE_OneParam(FGenInitialStateDelegate, StateType& State);
        FGenInitialStateDelegate GenerateInitialStatePTDelegate;

        /**
         * Broadcasted on the game thread on the auto proxy (or authority if there is no auto proxy) to inject input into the simulation. Inputs generated from this delegate
         * will be sent to the authority.
         * @param [out] Input The generated input.
         */
        DECLARE_MULTICAST_DELEGATE_OneParam(FInputGTDelegate, InputType& Input);
        FInputGTDelegate ProduceInputGTDelegate;

        /**
         * Broadcasted on the physics thread once per input sampled with ProduceInputGTDelegate on either the autonomous proxy or the authority if there is no autonomous proxy.
         * This delegate gives an opportunity to change the input based on the state of the simulation BEFORE it is sent to the authority. The auto proxy will use the modified
         * input itself as well to ensure that they remain in sync.
         * @param [out] Input The input to modify.
         * @param [in] PrevStateThe state of the simulation from the previous tick. This can be considered the current state of the simulation.
         * @param [in] TickInfo Information about the current tick, including the updated component.
         */
        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputPtDelegate, InputType& Input, const StateType& PrevState, const FSimTickInfo& TickInfo);
        FInputPtDelegate ModifyInputPTDelegate;

        /**
         * Broadcasted on the physics thread after input has been sampled and modified on the authority and auto proxy. Here is where you should apply any physics 
         * or perform any updates on the current state.
         * @param [in] TickInfo Information about the current tick, including the updated component.
         * @param [in] Input The input to use for this simulation tick (after it has been modified with ModifyInputPTDelegate).
         * @param [in] PrevState The state of the simulation from the previous tick. This can be considered the current state of the simulation.
         * @param [out] Output The updated state of the simulation for this tick.
         */
        DECLARE_MULTICAST_DELEGATE_FourParams(FSimTickDelegate, const FSimTickInfo& TickInfo, const InputType& Input, const StateType& PrevState, TickOutput& Output);
        FSimTickDelegate SimTickPrePhysicsDelegate;
        FSimTickDelegate SimTickPostPhysicsDelegate;

        /**
         * Broadcasted on the game thread on the authority and sim and auto proxies every frame. Here is where you should apply the simulation state to your component / actor.
         * @param [in] State The current state of the simulation on the game thread. This is interpolated between physics ticks.
         * @param [in] Dt The current delta time on the game thread.
         */
        DECLARE_MULTICAST_DELEGATE_TwoParams(FFinalizeDelegate, const StateType& State, Chaos::FReal Dt)
        FFinalizeDelegate FinalizeDelegate;

        /**
         * Broadcasted on the game thread on simulated proxies to extrapolate the simulation when the interpolation buffer is empty.
         * @param [in, out] State The state to extrapolate from.
         * @param [in] PrevState The latest received state before @param State.
         * @param [in] StateDt The delta time between @param State and @param PrevState.
         * @param [in] ExtrapolationTime The amount of time to extrapolate for.
         */
        DECLARE_MULTICAST_DELEGATE_FourParams(FExtrapolateDelegate, StateType& State, const StateType& PrevState, Chaos::FReal StateDt, Chaos::FReal ExtrapolationTime)
        FExtrapolateDelegate ExtrapolateDelegate;

        /**
         * Called on the physics thread on only the authority, returning true will end the simulation. There will be a slight delay between returning true
         * and the simulation actually ending on the game thread because physics ticks are buffered and then interpolated on the game thread. 
         * @param [in] TickInfo Information about the current tick, including the updated component.
         * @param [in] State The current state of the simulation.  
         */
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
