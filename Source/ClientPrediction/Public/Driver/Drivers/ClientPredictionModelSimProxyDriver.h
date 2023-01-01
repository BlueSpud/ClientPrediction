#pragma once

#include "ClientPredictionModelAuthDriver.h"
#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "Logging/LogMacros.h"
#include "World/ClientPredictionWorldManager.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyDelay;
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyTimeDilationMargin;
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyAggressiveTimeDilationMargin;
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyTimeDilation;
    extern CLIENTPREDICTION_API float ClientPredictionSimProxyAggressiveTimeDilation;

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
        void GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState);

        void DispatchEvents(Chaos::FReal StartTime, Chaos::FReal EndTime);
        void Finalize(Chaos::FReal Dt);
        void ApplyPhysicsState();

        void UpdateTimescale();
        Chaos::FReal GetTimeLeftInBuffer() const;
        void TrimStateBuffer();

    private:
        UPrimitiveComponent* UpdatedComponent = nullptr;
        IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;
        TArray<FStateWrapper<StateType>> States;

        /** This is the tick that was first received from the authority. */
        int32 StartingTick = INDEX_NONE;

        /** If the buffer is empty this is the state that will be used. */
        FStateWrapper<StateType> CurrentState = {};
        Chaos::FReal WorldStartTime = -1.0;
        Chaos::FReal LastWorldTime = -1.0;

        Chaos::FReal CurrentTime = -ClientPredictionSimProxyDelay;
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
        CurrentState = StartState;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::BindToRepProxy(FRepProxy& SimProxyRep) {
        SimProxyRep.SerializeFunc = [&](FArchive& Ar) {
            FStateWrapper<StateType> State{};
            State.NetSerialize(Ar, false);

            QueueState(State);
        };
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) { QueueState(State); }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::QueueState(const FStateWrapper<StateType>& State) {
        if (StartingTick == INDEX_NONE) {
            StartingTick = State.TickNumber;
        }

        FStateWrapper<StateType> StateWithTimes = State;
        BuildStateTimes(StateWithTimes);

        // If the state is in the past, any events that were included need to be dispatched
        if (StateWithTimes.StartTime <= CurrentTime) {
            Delegate->DispatchEvents(StateWithTimes, StateWithTimes.Events);
        }

        const bool bAlreadyHasState = States.ContainsByPredicate([&](const auto& Candidate) {
            return Candidate.TickNumber == State.TickNumber;
        });

        if (!bAlreadyHasState) {
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
        const Chaos::FReal LastTime = CurrentTime;
        CurrentTime += WorldDt * Timescale;

        DispatchEvents(LastTime, CurrentTime);

        if (States.IsEmpty()) {
            Finalize(WorldDt);
            return;
        }

        GetInterpolatedStateAssumingStatesNotEmpty(CurrentState);
        Finalize(WorldDt);

        UpdateTimescale();
        TrimStateBuffer();
    }

    template <typename InputType, typename StateType>
    Chaos::FReal FModelSimProxyDriver<InputType, StateType>::GetWorldDt() {
        const Chaos::FReal AbsoluteWorldTime = Delegate->GetWorldTimeNoDilation();
        Chaos::FReal WorldTime = AbsoluteWorldTime - WorldStartTime;

        if (WorldStartTime == -1.0) {
            WorldStartTime = AbsoluteWorldTime;
            LastWorldTime = WorldStartTime;

            WorldTime = 0.0;
        }

        // We don't use the delta from the function call because that is subject to physics time dilation
        const Chaos::FReal WorldDt = WorldTime - LastWorldTime;
        LastWorldTime = WorldTime;

        return WorldDt;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::GetInterpolatedStateAssumingStatesNotEmpty(FStateWrapper<StateType>& OutState) {
        for (int32 i = 0; i < States.Num(); i++) {
            if (CurrentTime < States[i].EndTime) {
                if (i == 0) {
                    OutState = States[0];
                    return;
                }

                const FStateWrapper<StateType>& Start = States[i - 1];
                const FStateWrapper<StateType>& End = States[i];
                OutState = Start;

                const Chaos::FReal PrevEndTime = Start.EndTime;
                const Chaos::FReal TimeFromPrevEnd = CurrentTime - PrevEndTime;
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
                Delegate->DispatchEvents(State, State.Events);
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
    void FModelSimProxyDriver<InputType, StateType>::UpdateTimescale() {
        const Chaos::FReal TimeLeftInBuffer = GetTimeLeftInBuffer();
        const Chaos::FReal PercentageDifference = (TimeLeftInBuffer - ClientPredictionSimProxyDelay) / ClientPredictionSimProxyDelay;

        Chaos::FReal TargetTimescale = 1.0;
        if (PercentageDifference >= ClientPredictionSimProxyTimeDilationMargin) {
            TargetTimescale = 1.0 + (PercentageDifference >= ClientPredictionSimProxyAggressiveTimeDilationMargin
                                         ? ClientPredictionSimProxyAggressiveTimeDilation
                                         : ClientPredictionSimProxyTimeDilation);
        }

        if (PercentageDifference <= ClientPredictionSimProxyTimeDilationMargin) {
            TargetTimescale = 1.0 - (PercentageDifference <= ClientPredictionSimProxyAggressiveTimeDilationMargin
                                         ? ClientPredictionSimProxyAggressiveTimeDilation
                                         : ClientPredictionSimProxyTimeDilation);
        }

        Timescale = FMath::Lerp(Timescale, TargetTimescale, ClientPredictionTimeDilationAlpha);
    }

    template <typename InputType, typename StateType>
    Chaos::FReal FModelSimProxyDriver<InputType, StateType>::GetTimeLeftInBuffer() const {
        if (States.IsEmpty()) { return 0.0; }

        const FStateWrapper<StateType>& LastBufferState = States.Last();
        if (LastBufferState.EndTime <= CurrentTime) { return 0.0; }

        return LastBufferState.EndTime - CurrentTime;
    }

    template <typename InputType, typename StateType>
    void FModelSimProxyDriver<InputType, StateType>::TrimStateBuffer() {
        while (States.Num() > 1) {
            // If time has progressed past the end time of the next state, then the first state in the buffer is no longer needed for interpolation
            if (States[1].EndTime < CurrentTime) {
                States.RemoveAt(0);
            }
            else { break; }
        }
    }
}
