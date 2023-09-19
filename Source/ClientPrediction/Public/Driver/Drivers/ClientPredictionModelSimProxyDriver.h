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

    private:
        void QueueState(const FStateWrapper<StateType>& State);

    public:
        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    private:
        void GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState, bool& bIsFinalState);

        void DispatchEvents(Chaos::FReal StartTime, Chaos::FReal EndTime);
        void Finalize(Chaos::FReal Dt);
        void ApplyPhysicsState();

        void TrimStateBuffer();

    private:
        UPrimitiveComponent* UpdatedComponent = nullptr;
        ECollisionEnabled::Type CachedCollisionMode = ECollisionEnabled::NoCollision;
        bool bCachedPhysicsEnabled = false;

        IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;
        TArray<FStateWrapper<StateType>> States;

        const UClientPredictionSettings* Settings = nullptr;

        /** If the buffer is empty this is the state that will be used. */
        FStateWrapper<StateType> CurrentState = {};
        bool bHasSeenFinalState = false;

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
    void FModelSimProxyDriver<InputType, StateType>::QueueState(const FStateWrapper<StateType>& State) {
        // If the state is in the past, any events that were included need to be dispatched
        if (State.StartTime <= InterpolationTime) {
            Delegate->DispatchEvents(State, State.Events, 0.0);
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
        if (StateManager == nullptr || bHasSeenFinalState) { return; }

        const Chaos::FReal LastInterpolationTime = InterpolationTime;
        InterpolationTime = StateManager->GetInterpolationTime();

        DispatchEvents(LastInterpolationTime, InterpolationTime);

        if (States.IsEmpty()) {
            Finalize(Dt);
            return;
        }

        GetInterpolatedStateAssumingStatesNotEmpty(CurrentState, bHasSeenFinalState);
        Finalize(Dt);

        TrimStateBuffer();

        if (bHasSeenFinalState) {
            if (Settings->bDisableCollisionsOnSimProxies) {
                UpdatedComponent->SetSimulatePhysics(bCachedPhysicsEnabled);
                UpdatedComponent->SetCollisionEnabled(CachedCollisionMode);
            }

            Delegate->EndSimulation();
        }
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState, bool& bIsFinalState) {
        if (States.Num() == 1) {
            OutState = States[0];
            bIsFinalState = States[0].bIsFinalState;

            return;
        }

        for (int32 i = 0; i < States.Num() - 1; i++) {
            if (InterpolationTime >= States[i].StartTime && InterpolationTime <= States[i + 1].StartTime) {
                const FStateWrapper<StateType>& Start = States[i];
                const FStateWrapper<StateType>& End = States[i + 1];
                check(!Start.bIsFinalState);

                OutState = Start;

                const Chaos::FReal TimeFromStart = InterpolationTime - Start.StartTime;
                const Chaos::FReal TotalTime = End.StartTime - Start.StartTime;

                if (TotalTime > 0.0) {
                    const Chaos::FReal Alpha = FMath::Clamp(TimeFromStart / TotalTime, 0.0, 1.0);
                    OutState.Interpolate(End, Alpha);
                }

                return;
            }
        }

        OutState = States.Last();
        bIsFinalState = States.Last().bIsFinalState;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::DispatchEvents(Chaos::FReal StartTime, Chaos::FReal EndTime) {
        for (const FStateWrapper<StateType>& State : States) {
            if (State.StartTime > StartTime && State.StartTime <= EndTime) {
                Delegate->DispatchEvents(State, State.Events, 0.0);
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

        if (UpdatedComponent->IsSimulatingPhysics()) {
            bCachedPhysicsEnabled = true;
        }

        UpdatedComponent->SetSimulatePhysics(false);

        if (Settings->bDisableCollisionsOnSimProxies) {
            const ECollisionEnabled::Type CurrentCollisionMode = UpdatedComponent->GetCollisionEnabled();
            if (CurrentCollisionMode != ECollisionEnabled::NoCollision) {
                CachedCollisionMode = CurrentCollisionMode;
            }

            UpdatedComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        UpdatedComponent->SetWorldLocation(CurrentState.PhysicsState.X);
        UpdatedComponent->SetWorldRotation(CurrentState.PhysicsState.R);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::TrimStateBuffer() {
        while (States.Num() >= 2) {
            // If time has progressed past the end time of the next state, then the first state in the buffer is no longer needed for interpolation
            if (States[1].EndTime < InterpolationTime) {
                States.RemoveAt(0);
            }
            else { break; }
        }
    }
}
