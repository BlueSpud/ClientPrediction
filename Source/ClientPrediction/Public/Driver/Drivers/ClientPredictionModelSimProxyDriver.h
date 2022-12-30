﻿#pragma once

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "World/ClientPredictionWorldManager.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyDelay;

    template <typename InputType, typename StateType>
    class FModelSimProxyDriver final : public IModelDriver<InputType, StateType> {
    public:
        FModelSimProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FRepProxy& SimProxyRep);

    private:
        void BindToRepProxy(FRepProxy& SimProxyRep);

    public:
        virtual void ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) override;

    private:
        void QueueState(const FStateWrapper<StateType>& State);
        void BuildStateTimes(FStateWrapper<StateType>& State);

    public:
        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    private:
        Chaos::FReal GetWorldDt();
        Chaos::FReal GetTimeLeftInBuffer(Chaos::FReal Start) const;
        void GetInterpolatedStateAssumingStatesNotEmpty(Chaos::FReal DelayedTime, FStateWrapper<StateType>& OutState);
        void Finalize(Chaos::FReal Dt);
        void ApplyPhysicsState();

    private:
        UPrimitiveComponent* UpdatedComponent = nullptr;
        IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;
        TArray<FStateWrapper<StateType>> States;

        /** This is the tick that was first received from the authority. */
        int32 StartingTick = INDEX_NONE;
        Chaos::FReal StartTime = -1.0;

        /** If the buffer is empty this is the state that will be used. */
        FStateWrapper<StateType> LastState = {};
        Chaos::FReal LastWorldTime = -1.0;

        Chaos::FReal CurrentTime = 0.0;
        Chaos::FReal Timescale = 1.0;
    };

    template <typename InputType, typename StateType>
    FModelSimProxyDriver<InputType, StateType>::FModelSimProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                                                                     FRepProxy& SimProxyRep) : UpdatedComponent(UpdatedComponent), Delegate(Delegate) {
        check(Delegate);
        BindToRepProxy(SimProxyRep);

        FStateWrapper<StateType> StartState{};
        Delegate->GenerateInitialState(StartState);

        StartState.TickNumber = INDEX_NONE;
        StartState.InputPacketTickNumber = INDEX_NONE;
        StartState.Events = 0;
        StartState.StartTime = 0.0;
        StartState.EndTime = 0.0;

        States.Add(StartState);
        LastState = StartState;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::BindToRepProxy(FRepProxy& SimProxyRep) {
        SimProxyRep.SerializeFunc = [&](FArchive& Ar) {
            FStateWrapper<StateType> State{};
            State.NetSerialize(Ar);

            QueueState(State);
        };
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) { QueueState(State); }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::QueueState(const FStateWrapper<StateType>& State) {
        if (StartingTick == INDEX_NONE) {
            StartingTick = State.TickNumber;

            FStateWrapper<StateType> StateWithTimes = State;
            BuildStateTimes(StateWithTimes);
            States.Add(StateWithTimes);

            return;
        }

        // TODO Check if state is too old
        const bool bAlreadyHasState = States.ContainsByPredicate([&](const auto& Candidate) {
            return Candidate.TickNumber == State.TickNumber;
        });

        if (!bAlreadyHasState) {
            FStateWrapper<StateType> StateWithTimes = State;
            BuildStateTimes(StateWithTimes);

            States.Add(StateWithTimes);
            States.Sort([](const auto& A, const auto& B) { return A.TickNumber < B.TickNumber; });
        }
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::BuildStateTimes(FStateWrapper<StateType>& State) {
        State.StartTime = static_cast<Chaos::FReal>(State.TickNumber - StartingTick) * ClientPredictionFixedDt;
        State.EndTime = State.StartTime + ClientPredictionFixedDt;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        if (StartingTick == INDEX_NONE) { return; }

        const Chaos::FReal WorldDt = GetWorldDt();
        CurrentTime += WorldDt * Timescale;

        const Chaos::FReal DelayedTime = CurrentTime - ClientPredictionSimProxyDelay;
        if (States.IsEmpty()) {
            Finalize(WorldDt);
            return;
        }

        GetInterpolatedStateAssumingStatesNotEmpty(DelayedTime, LastState);
        Finalize(WorldDt);
    }

    template <typename InputType, typename StateType>
    Chaos::FReal FModelSimProxyDriver<InputType, StateType>::GetWorldDt() {
        const Chaos::FReal AbsoluteWorldTime = Delegate->GetWorldTimeNoDilation();
        Chaos::FReal WorldTime = AbsoluteWorldTime - StartTime;

        if (StartTime == -1.0) {
            StartTime = AbsoluteWorldTime;
            LastWorldTime = StartTime;

            WorldTime = 0.0;
        }

        // We don't use the delta from the function call because that is subject to physics time dilation
        const Chaos::FReal WorldDt = WorldTime - LastWorldTime;
        LastWorldTime = WorldTime;

        return WorldDt;
    }

    template <typename InputType, typename StateType>
    Chaos::FReal FModelSimProxyDriver<InputType, StateType>::GetTimeLeftInBuffer(Chaos::FReal Start) const {
        if (States.IsEmpty()) { return 0.0; }

        const FStateWrapper<StateType>& LastState = States.Last();
        if (LastState.EndTime <= Start) { return 0.0; }

        return LastState.EndTime - Start;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::GetInterpolatedStateAssumingStatesNotEmpty(Chaos::FReal DelayedTime, FStateWrapper<StateType>& OutState) {
        for (int32 i = 0; i < States.Num(); i++) {
            if (DelayedTime < States[i].EndTime) {
                if (i == 0) {
                    OutState = States[0];
                    return;
                }

                const FStateWrapper<StateType>& Start = States[i - 1];
                const FStateWrapper<StateType>& End = States[i];
                OutState = Start;

                const Chaos::FReal PrevEndTime = Start.EndTime;
                const Chaos::FReal TimeFromPrevEnd = DelayedTime - PrevEndTime;
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
    void FModelSimProxyDriver<InputType, StateType>::Finalize(Chaos::FReal Dt) {
        ApplyPhysicsState();
        Delegate->Finalize(LastState.Body, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ApplyPhysicsState() {
        check(UpdatedComponent);

        Chaos::FRigidBodyHandle_External& Handle = UpdatedComponent->BodyInstance.ActorHandle->GetGameThreadAPI();
        Handle.SetObjectState(Chaos::EObjectStateType::Static);
        Handle.SetX(LastState.PhysicsState.X);
        Handle.SetR(LastState.PhysicsState.R);
    }
}