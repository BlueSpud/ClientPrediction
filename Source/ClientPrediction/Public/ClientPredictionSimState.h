#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionModelTypes.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    struct FPhysState {
        /** These mirror the Chaos properties for a particle */
        Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

        Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
        Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

        Chaos::FRotation3 R = Chaos::FRotation3::Identity;
        Chaos::FVec3 W = Chaos::FVec3::ZeroVector;

        bool ShouldReconcile(const FPhysState& State) const {
            if (State.ObjectState != ObjectState) { return true; }
            if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
            if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
            if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
            if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

            return false;
        }
    };

    template <typename StateType>
    struct FWrappedState {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        StateType State{};
        FPhysState PhysState{};

        // These are not sent over the network, they're used for local interpolation only
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;
    };

    class USimStateBase {
    public:
        virtual ~USimStateBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitSimProxyStateBundleDelegate, const FBundledPackets& Bundle)
        FEmitSimProxyStateBundleDelegate EmitAutoProxyBundle;
        FEmitSimProxyStateBundleDelegate EmitSimProxyBundle;

        virtual void ConsumeStateBundle(const FBundledPackets& Packets) = 0;
    };

    template <typename Traits>
    class USimState : public USimStateBase {
    private:
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;
        using WrappedState = typename FWrappedState<StateType>;

    public:
        virtual ~USimState() override = default;
        void SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;

    public:
        virtual void ConsumeStateBundle(const FBundledPackets& Packets) override;
        void TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input);
        void TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input);
        int32 GetRewindTick();

    private:
        static void FillStateSimDetails(WrappedState& State, const FNetTickInfo& TickInfo);
        static void FillStatePhysInfo(WrappedState& State, const FNetTickInfo& TickInfo);
        void GenerateInitialState(const FNetTickInfo& TickInfo);

    private:
        bool bGeneratedInitialState = false;
        TArray<WrappedState> StateHistory;

        WrappedState PrevState{};
        WrappedState CurrentState{};

        // Relevant only for auto proxies
        WrappedState LastAuthorityState{};
        int32 LatestAckedServerTick = INDEX_NONE;
    };

    template <typename Traits>
    void USimState<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimState<Traits>::ConsumeStateBundle(const FBundledPackets& Packets) {
        
    }

    template <typename Traits>
    void USimState<Traits>::TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (!bGeneratedInitialState) {
            GenerateInitialState(TickInfo);
            bGeneratedInitialState = true;
        }

        if (TickInfo.SimRole == ROLE_SimulatedProxy) {
            return;
        }

        const int32 PrevTickNumber = TickInfo.LocalTick - 1;
        for (const WrappedState& State : StateHistory) {
            if (State.LocalTick > PrevTickNumber) { break; }

            PrevState = State;
            if (State.LocalTick == PrevTickNumber) { break; }
        }

        CurrentState.State = PrevState.State;
    }

    template <typename Traits>
    void USimState<Traits>::TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (TickInfo.SimRole == ROLE_SimulatedProxy) { return; }

        USimState::FillStateSimDetails(CurrentState, TickInfo);
        if (StateHistory.IsEmpty() || StateHistory.Last().LocalTick < TickInfo.LocalTick) {
            StateHistory.Add(MoveTemp(CurrentState));
            return;
        }

        for (WrappedState& State : StateHistory) {
            if (State.LocalTick == TickInfo.LocalTick) {
                State = CurrentState;
                return;
            }
        }
    }

    template <typename Traits>
    int32 USimState<Traits>::GetRewindTick() {
        if (LastAuthorityState.ServerTick == INDEX_NONE || LastAuthorityState.ServerTick <= LatestAckedServerTick) { return INDEX_NONE; }

        WrappedState* HistoricState = nullptr;
        for (WrappedState& State : StateHistory) {
            if (State.ServerTick == LastAuthorityState.ServerTick) {
                HistoricState = &State;

                break;
            }
        }

        if (HistoricState == nullptr || !HistoricState->PhysState.ShouldReconcile(LastAuthorityState.PhysState)) {
            return INDEX_NONE;
        }

        UE_LOG(LogTemp, Error, TEXT("Performing correction on %d (Server tick %d)"), HistoricState->LocalTick, HistoricState->ServerTick);

        const int32 RollbackTick = HistoricState->LocalTick + 1;
        return RollbackTick;
    }

    template <typename Traits>
    void USimState<Traits>::FillStateSimDetails(WrappedState& State, const FNetTickInfo& TickInfo) {
        if (TickInfo.UpdatedComponent == nullptr) { return; }

        State.LocalTick = TickInfo.LocalTick;
        State.ServerTick = TickInfo.ServerTick;

        State.StartTime = TickInfo.StartTime;
        State.EndTime = TickInfo.EndTime;

        USimState::FillStatePhysInfo(State, TickInfo);
    }

    template <typename Traits>
    void USimState<Traits>::FillStatePhysInfo(WrappedState& State, const FNetTickInfo& TickInfo) {
        FBodyInstance* BodyInstance = TickInfo.UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) {
            State.PhysState.ObjectState = Chaos::EObjectStateType::Uninitialized;
            return;
        }

        const Chaos::FRigidBodyHandle_Internal* Handle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
        State.PhysState.ObjectState = Handle->ObjectState();
        State.PhysState.X = Handle->X();
        State.PhysState.V = Handle->V();
        State.PhysState.R = Handle->R();
        State.PhysState.W = Handle->W();
    }

    template <typename Traits>
    void USimState<Traits>::GenerateInitialState(const FNetTickInfo& TickInfo) {
        if (SimDelegates == nullptr) { return; }

        // We can leave the frame indexes and times as invalid because this is just a starting off point until we get the first valid frame
        WrappedState NewState{};
        USimState::FillStatePhysInfo(NewState, TickInfo);

        SimDelegates->GenerateInitialStatePTDelegate.Broadcast(NewState.State);
        StateHistory.Add(MoveTemp(NewState));
    }
}
