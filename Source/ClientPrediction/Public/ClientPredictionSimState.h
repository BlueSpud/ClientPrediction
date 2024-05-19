#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Chaos/PhysicsObjectInternalInterface.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionModelTypes.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    struct FPhysState {
        /** These mirror the Chaos properties for a particle */
        Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

        Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
        Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

        Chaos::FRotation3 R = Chaos::FRotation3::Identity;
        Chaos::FVec3 W = Chaos::FVec3::ZeroVector;

        bool ShouldReconcile(const FPhysState& State) const;
        void NetSerialize(FArchive& Ar, EDataCompleteness Completeness);
    };

    inline bool FPhysState::ShouldReconcile(const FPhysState& State) const {
        if (State.ObjectState != ObjectState) { return true; }
        if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
        if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
        if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
        if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

        return false;
    }

    inline void FPhysState::NetSerialize(FArchive& Ar, EDataCompleteness Completeness) {
        if (Completeness == EDataCompleteness::kLow) {
            SerializeHalfPrecision(X, Ar);
            SerializeHalfPrecision(R, Ar);

            return;
        }

        Ar << ObjectState;

        // Serialize manually to make sure that they are serialized as doubles
        Ar << X.X;
        Ar << X.Y;
        Ar << X.Z;

        Ar << V.X;
        Ar << V.Y;
        Ar << V.Z;

        Ar << R.X;
        Ar << R.Y;
        Ar << R.Z;
        Ar << R.W;

        Ar << W.X;
        Ar << W.Y;
        Ar << W.Z;
    }

    template <typename StateType>
    struct FWrappedState {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        StateType State{};
        FPhysState PhysState{};

        // These are not sent over the network, they're used for local interpolation only
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        void NetSerialize(FArchive& Ar, EDataCompleteness Completeness);
    };

    template <typename StateType>
    void FWrappedState<StateType>::NetSerialize(FArchive& Ar, EDataCompleteness Completeness) {
        Ar << ServerTick;
        PhysState.NetSerialize(Ar, Completeness);
        State.NetSerialize(Ar, Completeness);
    }

    class USimStateBase {
    public:
        virtual ~USimStateBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitSimProxyStateBundleDelegate, const FBundledPacketsLow& Bundle)
        FEmitSimProxyStateBundleDelegate EmitSimProxyBundle;

        DECLARE_DELEGATE_OneParam(FEmitAutoProxyStateBundleDelegate, const FBundledPacketsFull& Bundle)
        FEmitAutoProxyStateBundleDelegate EmitAutoProxyBundle;

        virtual void ConsumeSimProxyStates(const FBundledPacketsLow& Packets) = 0;
        virtual void ConsumeAutoProxyStates(const FBundledPacketsFull& Packets) = 0;
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
        virtual void ConsumeSimProxyStates(const FBundledPacketsLow& Packets) override;
        virtual void ConsumeAutoProxyStates(const FBundledPacketsFull& Packets) override;

    private:
        static void FillStateSimDetails(WrappedState& State, const FNetTickInfo& TickInfo);
        static void FillStatePhysInfo(WrappedState& State, const FNetTickInfo& TickInfo);
        void GenerateInitialState(const FNetTickInfo& TickInfo);

        static Chaos::FRigidBodyHandle_Internal* GetPhysHandle(const FNetTickInfo& TickInfo);

    public:
        void TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input);
        void TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input);

        int32 GetRewindTick(Chaos::FPhysicsSolver* PhysSolver, Chaos::FPhysicsObjectHandle PhysObject);
        void ApplyCorrectionIfNeeded(const FNetTickInfo& TickInfo);

        void EmitStates(int32 LatestTick);

    private:
        bool bGeneratedInitialState = false;
        TArray<WrappedState> StateHistory;

        WrappedState PrevState{};
        WrappedState CurrentState{};

        // Relevant only for auto proxies
        WrappedState LatestAuthorityState{};
        int32 LatestAckedServerTick = INDEX_NONE;
        TOptional<WrappedState> PendingCorrection;

        // Relevant only for the authorities
        int32 LatestEmittedTick = INDEX_NONE;
    };

    template <typename Traits>
    void USimState<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimState<Traits>::ConsumeSimProxyStates(const FBundledPacketsLow& Packets) {}

    template <typename Traits>
    void USimState<Traits>::ConsumeAutoProxyStates(const FBundledPacketsFull& Packets) {
        TArray<WrappedState> AuthorityStates;
        Packets.Bundle().Retrieve(AuthorityStates);

        if (AuthorityStates.IsEmpty()) { return; }
        LatestAuthorityState = AuthorityStates.Last();
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
        const Chaos::FRigidBodyHandle_Internal* Handle = GetPhysHandle(TickInfo);
        if (Handle == nullptr) {
            State.PhysState.ObjectState = Chaos::EObjectStateType::Uninitialized;
            return;
        }

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

    template <typename Traits>
    Chaos::FRigidBodyHandle_Internal* USimState<Traits>::GetPhysHandle(const FNetTickInfo& TickInfo) {
        FBodyInstance* BodyInstance = TickInfo.UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) { return nullptr; }

        return BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
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

        ApplyCorrectionIfNeeded(TickInfo);

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
    int32 USimState<Traits>::GetRewindTick(Chaos::FPhysicsSolver* PhysSolver, Chaos::FPhysicsObjectHandle PhysObject) {
        if (LatestAuthorityState.ServerTick == INDEX_NONE || LatestAuthorityState.ServerTick <= LatestAckedServerTick) { return INDEX_NONE; }
        LatestAckedServerTick = LatestAuthorityState.ServerTick;

        Chaos::FRewindData* RewindData = PhysSolver->GetRewindData();
        if (RewindData == nullptr) { return INDEX_NONE; }

        WrappedState* HistoricState = nullptr;
        for (WrappedState& State : StateHistory) {
            if (State.ServerTick == LatestAuthorityState.ServerTick) {
                HistoricState = &State;

                break;
            }
        }

        if (HistoricState == nullptr || !HistoricState->PhysState.ShouldReconcile(LatestAuthorityState.PhysState)) {
            return INDEX_NONE;
        }

        // Resimulating frames that were already once resimulated can be disallowed, so we ignore any corrections that would result in no resim
        const int32 RewindTick = HistoricState->LocalTick;
        const int32 BlockedResimTick = RewindData->GetBlockedResimFrame();
        if (BlockedResimTick != INDEX_NONE && RewindTick <= BlockedResimTick) {
            return INDEX_NONE;
        }

        PendingCorrection = LatestAuthorityState;
        PendingCorrection->LocalTick = RewindTick;

        HistoricState->PhysState = LatestAuthorityState.PhysState;
        HistoricState->State = LatestAuthorityState.State;

        Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
        if (Chaos::FPBDRigidParticleHandle* POHandle = Interface.GetRigidParticle(PhysObject)) {
            PhysSolver->GetEvolution()->GetIslandManager().SetParticleResimFrame(POHandle, RewindTick);
        }

        RewindData->SetResimFrame(FMath::Min(RewindTick, RewindData->GetResimFrame()));

        UE_LOG(LogTemp, Error, TEXT("Queueing correction on %d (Server tick %d)"), RewindTick, LatestAuthorityState.ServerTick);
        return RewindTick;
    }

    template <typename Traits>
    void USimState<Traits>::ApplyCorrectionIfNeeded(const FNetTickInfo& TickInfo) {
        if (!PendingCorrection.IsSet() || PendingCorrection->LocalTick != TickInfo.LocalTick) { return; }

        Chaos::FRigidBodyHandle_Internal* Handle = GetPhysHandle(TickInfo);
        if (Handle == nullptr) { return; }

        const FPhysState& PhysState = PendingCorrection->PhysState;
        Handle->SetObjectState(PhysState.ObjectState);
        Handle->SetX(PhysState.X);
        Handle->SetV(PhysState.V);
        Handle->SetR(PhysState.R);
        Handle->SetW(PhysState.W);

        UE_LOG(LogTemp, Log, TEXT("Applying correction on %d"), PendingCorrection->LocalTick);
        PendingCorrection.Reset();
    }

    template <typename Traits>
    void USimState<Traits>::EmitStates(int32 LatestTick) {
        if (StateHistory.IsEmpty() || LatestTick <= LatestEmittedTick) { return; }

        FBundledPacketsFull AutoProxyPackets{};
        TArray<WrappedState> AutoProxyStates = {StateHistory.Last()};
        AutoProxyPackets.Bundle().Store(AutoProxyStates);

        EmitAutoProxyBundle.ExecuteIfBound(AutoProxyPackets);
    }
}
