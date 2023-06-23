#pragma once

#include "Logging/LogMacros.h"

#include "ClientPredictionSettings.h"
#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"

namespace ClientPrediction {
    template <typename InputType, typename StateType>
    class FModelSimProxyDriver final : public IModelDriver<InputType, StateType>, public StateConsumerBase<FStateWrapper<StateType>> {
    public:
        FModelSimProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate);
        virtual ~FModelSimProxyDriver() = default;

        virtual void Register(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;
        virtual void Unregister(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;

        virtual void ConsumeUnserializedStateForTick(const int32 Tick, const FStateWrapper<StateType>& State, const Chaos::FReal ServerTime) override;
        virtual void ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) override;

    private:
        void QueueState(const FStateWrapper<StateType>& State);

    public:
        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    private:
        void GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState);

        void DispatchEvents(Chaos::FReal StartTime, Chaos::FReal EndTime);
        void Finalize(Chaos::FReal Dt);
        void ApplyPhysicsState();

        void TrimStateBuffer();

    private:
        UPrimitiveComponent* UpdatedComponent = nullptr;
        IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;
        TArray<FStateWrapper<StateType>> States;

        const UClientPredictionSettings* Settings = nullptr;

        /** If the buffer is empty this is the state that will be used. */
        FStateWrapper<StateType> CurrentState = {};

        FStateManager* StateManager = nullptr;
        Chaos::FReal InterpolationTime = 0.0;
    };

    template <typename InputType, typename StateType>
    FModelSimProxyDriver<InputType, StateType>::FModelSimProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate) :
        UpdatedComponent(UpdatedComponent), Delegate(Delegate),

        Settings(GetDefault<UClientPredictionSettings>()) {
        check(Delegate);

        FStateWrapper<StateType> StartState{};
        Delegate->GenerateInitialState(StartState);

        StartState.TickNumber = 0;
        StartState.InputPacketTickNumber = INDEX_NONE;
        StartState.Events = 0;
        StartState.StartTime = 0.0;
        StartState.EndTime = 0.0;

        States.Add(StartState);
        CurrentState = StartState;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::Register(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        IModelDriver<InputType, StateType>::Register(WorldManager, ModelId);

        StateManager = &WorldManager->GetStateManager();
        StateManager->RegisterConsumerForModel(ModelId, this);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::Unregister(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        IModelDriver<InputType, StateType>::Unregister(WorldManager, ModelId);

        StateManager->UnregisterConsumerForModel(ModelId);
        StateManager = nullptr;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ConsumeUnserializedStateForTick(const int32 Tick, const FStateWrapper<StateType>& State,
                                                                                     const Chaos::FReal ServerTime) {
        FStateWrapper<StateType> StateWithTimes = State;
        StateWithTimes.StartTime = ServerTime;
        StateWithTimes.EndTime = ServerTime + Settings->FixedDt;

        QueueState(StateWithTimes);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) {
        // TODO this needs to get the server time correctly
        // QueueState(State);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::QueueState(const FStateWrapper<StateType>& State) {
        // If the state is in the past, any events that were included need to be dispatched
        if (State.StartTime <= InterpolationTime) {
            Delegate->DispatchEvents(State, State.Events, 0.0, 0.0);
        }

        const bool bAlreadyHasState = States.ContainsByPredicate([&](const auto& Candidate) {
            return Candidate.TickNumber == State.TickNumber;
        });

        if (!bAlreadyHasState) {
            States.Add(State);
            States.Sort([](const auto& A, const auto& B) { return A.TickNumber < B.TickNumber; });
        }
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        if (StateManager == nullptr) { return; }

        const Chaos::FReal LastInterpolationTime = InterpolationTime;
        InterpolationTime = StateManager->GetInterpolationTime();

        DispatchEvents(LastInterpolationTime, InterpolationTime);

        if (States.IsEmpty()) {
            Finalize(Dt);
            return;
        }

        GetInterpolatedStateAssumingStatesNotEmpty(CurrentState);
        Finalize(Dt);

        TrimStateBuffer();
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState) {
        for (int32 i = 0; i < States.Num(); i++) {
            if (InterpolationTime < States[i].EndTime) {
                if (i == 0) {
                    OutState = States[0];
                    return;
                }

                const FStateWrapper<StateType>& Start = States[i - 1];
                const FStateWrapper<StateType>& End = States[i];
                OutState = Start;

                const Chaos::FReal PrevEndTime = Start.EndTime;
                const Chaos::FReal TimeFromPrevEnd = InterpolationTime - PrevEndTime;
                const Chaos::FReal TotalTime = End.EndTime - Start.EndTime;

                if (TotalTime > 0.0) {
                    const Chaos::FReal Alpha = FMath::Clamp(TimeFromPrevEnd / TotalTime, 0.0, 1.0);
                    OutState.Interpolate(End, Alpha);
                }

                return;
            }
        }

        OutState = States.Last();
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::DispatchEvents(Chaos::FReal StartTime, Chaos::FReal EndTime) {
        for (const FStateWrapper<StateType>& State : States) {
            if (State.StartTime > StartTime && State.StartTime <= EndTime) {
                Delegate->DispatchEvents(State, State.Events, 0.0, 0.0);
            }
        }
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::Finalize(Chaos::FReal Dt) {
        ApplyPhysicsState();
        Delegate->Finalize(CurrentState.Body, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ApplyPhysicsState() {
        check(UpdatedComponent);

        UpdatedComponent->SetSimulatePhysics(false);
        UpdatedComponent->SetWorldLocation(CurrentState.PhysicsState.X);
        UpdatedComponent->SetWorldRotation(CurrentState.PhysicsState.R);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::TrimStateBuffer() {
        while (States.Num() > 1) {
            // If time has progressed past the end time of the next state, then the first state in the buffer is no longer needed for interpolation
            if (States[1].EndTime < InterpolationTime) {
                States.RemoveAt(0);
            }
            else { break; }
        }
    }
}
