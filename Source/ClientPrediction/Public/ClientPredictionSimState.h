#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Chaos/PhysicsObjectInternalInterface.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionModelTypes.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionSimEvents.h"
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
        void Interpolate(const FPhysState& Other, const Chaos::FReal Alpha);
    };

    inline bool FPhysState::ShouldReconcile(const FPhysState& State) const {
        if (State.ObjectState != ObjectState) { return true; }
        if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
        if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
        if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
        if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

        return false;
    }

    inline void FPhysState::Interpolate(const FPhysState& Other, const Chaos::FReal Alpha) {
        ObjectState = Other.ObjectState;
        X = FMath::Lerp(FVector(X), FVector(Other.X), Alpha);
        V = FMath::Lerp(FVector(V), FVector(Other.V), Alpha);
        R = FMath::Lerp(FQuat(R), FQuat(Other.R), Alpha);
        W = FMath::Lerp(FVector(W), FVector(Other.W), Alpha);
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
        void Interpolate(const FWrappedState& Other, Chaos::FReal Alpha);
    };

    template <typename StateType>
    void FWrappedState<StateType>::NetSerialize(FArchive& Ar, EDataCompleteness Completeness) {
        Ar << ServerTick;
        PhysState.NetSerialize(Ar, Completeness);
        State.NetSerialize(Ar, Completeness);
    }

    template <typename StateType>
    void FWrappedState<StateType>::Interpolate(const FWrappedState& Other, Chaos::FReal Alpha) {
        PhysState.Interpolate(Other.PhysState, Alpha);
        State.Interpolate(Other.State, Alpha);
    }

    class USimStateBase {
    public:
        virtual ~USimStateBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitSimProxyStateBundleDelegate, const FBundledPacketsLow& Bundle)
        FEmitSimProxyStateBundleDelegate EmitSimProxyBundle;

        DECLARE_DELEGATE_OneParam(FEmitAutoProxyStateBundleDelegate, const FBundledPacketsFull& Bundle)
        FEmitAutoProxyStateBundleDelegate EmitAutoProxyBundle;
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
        void SetSimEvents(const TSharedPtr<FSimEvents<Traits>>& NewSimEvents);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;
        TSharedPtr<FSimEvents<Traits>> SimEvents;

    public:
        int32 ConsumeSimProxyStates(const FBundledPacketsLow& Packets);
        void ConsumeAutoProxyStates(const FBundledPacketsFull& Packets);

    private:
        static void FillStateSimDetails(WrappedState& State, const FNetTickInfo& TickInfo);
        static void FillStatePhysInfo(WrappedState& State, const FNetTickInfo& TickInfo);
        void GenerateInitialState(const FNetTickInfo& TickInfo);

    public:
        void TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input);
        void TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input);

    private:
        void SimProxyAdjustStateTimeline(const FNetTickInfo& Info);

    public:
        int32 GetRewindTick(Chaos::FPhysicsSolver* PhysSolver, Chaos::FPhysicsObjectHandle PhysObject);
        void ApplyCorrectionIfNeeded(const FNetTickInfo& TickInfo);

        void EmitStates(int32 LatestTick);
        void InterpolateGameThread(UPrimitiveComponent* UpdatedComponent, Chaos::FReal ResultsTime, Chaos::FReal Dt, ENetRole SimRole);

    private:
        void GetInterpolatedStateAtTime(Chaos::FReal ResultsTime, WrappedState& OutState) const;
        void GetInterpolatedStateAtTimeSimProxy(Chaos::FReal ResultsTime, WrappedState& OutState) const;
        static Chaos::FRigidBodyHandle_Internal* GetPhysHandle(const FNetTickInfo& TickInfo);

    private:
        bool bGeneratedInitialState = false;
        TArray<WrappedState> StateHistory;

        WrappedState PrevState{};
        WrappedState CurrentState{};
        WrappedState LastInterpolatedState{};

        // Relevant only for sim proxies
        int32 SimProxyOffsetFromServer = INDEX_NONE;
        TArray<WrappedState> PendingSimProxyStates;
        ECollisionEnabled::Type CachedCollisionMode = ECollisionEnabled::NoCollision;

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
    void USimState<Traits>::SetSimEvents(const TSharedPtr<FSimEvents<Traits>>& NewSimEvents) {
        SimEvents = NewSimEvents;
    }

    template <typename Traits>
    int32 USimState<Traits>::ConsumeSimProxyStates(const FBundledPacketsLow& Packets) {
        TArray<WrappedState> AuthorityStates;
        Packets.Bundle().Retrieve(AuthorityStates);

        // We add the states to a queue so that we can update these states as well as the existing ones in SimProxyAdjustStateTimeline(). We can't do all that
        // logic in this function since the sim proxy offset might change in between recieves.
        int32 NewestState = INDEX_NONE;
        for (WrappedState& NewState : AuthorityStates) {
            NewestState = FMath::Max(NewestState, NewState.ServerTick);
            PendingSimProxyStates.Emplace(MoveTemp(NewState));
        }

        return NewestState;
    }

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
        USimState::FillStatePhysInfo(LastInterpolatedState, TickInfo);
        SimDelegates->GenerateInitialStatePTDelegate.Broadcast(LastInterpolatedState.State);

        StateHistory.Add(LastInterpolatedState);
    }

    template <typename Traits>
    void USimState<Traits>::TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (SimDelegates == nullptr) { return; }

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

        FTickOutput Output(CurrentState.State, SimEvents);
        SimDelegates->SimTickPrePhysicsDelegate.Broadcast(TickInfo, Input, PrevState.State, Output);
    }

    template <typename Traits>
    void USimState<Traits>::TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (TickInfo.SimRole == ROLE_SimulatedProxy) {
            SimProxyAdjustStateTimeline(TickInfo);
            return;
        }

        FTickOutput Output(CurrentState.State, SimEvents);
        SimDelegates->SimTickPostPhysicsDelegate.Broadcast(TickInfo, Input, PrevState.State, Output);

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
    void USimState<Traits>::SimProxyAdjustStateTimeline(const FNetTickInfo& Info) {
        const int32 OffsetFromServer = Info.SimProxyWorldManager->GetOffsetFromServer();
        auto UpdateState = [&](WrappedState& State) {
            State.LocalTick = State.ServerTick + OffsetFromServer;
            State.StartTime = static_cast<Chaos::FReal>(State.LocalTick + 1) * Info.Dt;
            State.EndTime = State.StartTime + Info.Dt;
        };

        Chaos::FReal StartTime = -100000.0;
        for (WrappedState& State : StateHistory) {
            check(State.StartTime > StartTime);
            StartTime = State.StartTime;
        }

        if (OffsetFromServer != SimProxyOffsetFromServer) {
            for (WrappedState& State : StateHistory) { UpdateState(State); }
        }

        if (PendingSimProxyStates.IsEmpty()) {
            return;
        }

        // TODO This can probably be optimized a bit
        for (WrappedState& NewState : PendingSimProxyStates) {
            if (StateHistory.ContainsByPredicate([&](const WrappedState& Other) { return Other.ServerTick == NewState.ServerTick; })) {
                continue;
            }

            UpdateState(NewState);
            StateHistory.Add(MoveTemp(NewState));
        }

        StateHistory.Sort([](const WrappedState& A, const WrappedState& B) { return A.ServerTick < B.ServerTick; });
        PendingSimProxyStates.Reset();
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

        if (HistoricState == nullptr) {
            return INDEX_NONE;
        }

        if (!HistoricState->PhysState.ShouldReconcile(LatestAuthorityState.PhysState) && !HistoricState->State.ShouldReconcile(LatestAuthorityState.State)) {
            return INDEX_NONE;
        }

        // Resimulating frames that were already once resimulated can be disallowed, so we ignore any corrections that would result in no resim.
        // We add one to the local tick since states are generated at the end of a tick and corrections are applied at the beginning. So if we didn't
        // add an offset we would end up simulating one extra tick.
        const int32 RewindTick = HistoricState->LocalTick + 1;
        const int32 BlockedResimTick = RewindData->GetBlockedResimFrame();
        if (BlockedResimTick != INDEX_NONE && RewindTick <= BlockedResimTick) {
            return INDEX_NONE;
        }

        PendingCorrection = LatestAuthorityState;
        PendingCorrection->LocalTick = RewindTick;

        HistoricState->PhysState = LatestAuthorityState.PhysState;
        HistoricState->State = LatestAuthorityState.State;

        Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
        if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysObject)) {
            PhysSolver->GetEvolution()->GetIslandManager().SetParticleResimFrame(ParticleHandle, RewindTick);
        }

        const int32 SolverResimTick = (RewindData->GetResimFrame() == INDEX_NONE) ? RewindTick : FMath::Min(RewindTick, RewindData->GetResimFrame());
        RewindData->SetResimFrame(SolverResimTick);

        UE_LOG(LogTemp, Warning, TEXT("Queueing correction on %d (Server tick %d)"), RewindTick, LatestAuthorityState.ServerTick);
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

    // TODO we don't need the latest emitted tick here, we can use the state history itself. 
    template <typename Traits>
    void USimState<Traits>::EmitStates(int32 LatestTick) {
        if (StateHistory.IsEmpty() || LatestTick <= LatestEmittedTick) { return; }

        FBundledPacketsFull AutoProxyPackets{};
        TArray<WrappedState> AutoProxyStates = {StateHistory.Last()};

        AutoProxyPackets.Bundle().Store(AutoProxyStates);
        EmitAutoProxyBundle.ExecuteIfBound(AutoProxyPackets);

        TArray<WrappedState> SimProxyStates;
        for (const WrappedState& State : StateHistory) {
            if (State.ServerTick > LatestEmittedTick && State.ServerTick <= LatestTick) {
                SimProxyStates.Add(State);
            }
        }

        FBundledPacketsLow SimProxyPackets{};
        SimProxyPackets.Bundle().Store(SimProxyStates);
        EmitSimProxyBundle.ExecuteIfBound(SimProxyPackets);

        LatestEmittedTick = LatestTick;
    }

    template <typename Traits>
    void USimState<Traits>::InterpolateGameThread(UPrimitiveComponent* UpdatedComponent, Chaos::FReal ResultsTime, Chaos::FReal Dt, ENetRole SimRole) {
        if (SimDelegates == nullptr) { return; }

        if (SimRole == ROLE_SimulatedProxy) {
            GetInterpolatedStateAtTimeSimProxy(ResultsTime, LastInterpolatedState);

            FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
            if (BodyInstance == nullptr) { return; }

            Chaos::FRigidBodyHandle_External& Handle = BodyInstance->GetPhysicsActorHandle()->GetGameThreadAPI();
            Handle.SetObjectState(Chaos::EObjectStateType::Kinematic);

            // TODO pull in the settings and make this conditional right
            if (true) {
                const ECollisionEnabled::Type CurrentCollisionMode = UpdatedComponent->GetCollisionEnabled();
                if (CurrentCollisionMode != ECollisionEnabled::QueryAndProbe) {
                    CachedCollisionMode = CurrentCollisionMode;
                }

                UpdatedComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndProbe);
            }

            Handle.SetX(LastInterpolatedState.PhysState.X);
            Handle.SetR(LastInterpolatedState.PhysState.R);
        }
        else { GetInterpolatedStateAtTime(ResultsTime, LastInterpolatedState); }

        SimDelegates->FinalizeDelegate.Broadcast(LastInterpolatedState.State, Dt);
    }

    template <typename Traits>
    void USimState<Traits>::GetInterpolatedStateAtTime(Chaos::FReal ResultsTime, WrappedState& OutState) const {
        if (StateHistory.IsEmpty()) {
            OutState = LastInterpolatedState;
            return;
        }

        for (int StateIndex = 0; StateIndex < StateHistory.Num(); StateIndex++) {
            if (StateHistory[StateIndex].EndTime < ResultsTime) { continue; }

            if (StateIndex == 0) {
                OutState = StateHistory[0];
                return;
            }

            const WrappedState& Start = StateHistory[StateIndex - 1];
            const WrappedState& End = StateHistory[StateIndex];
            OutState = Start;

            const Chaos::FReal Denominator = End.EndTime - End.StartTime;
            const Chaos::FReal Alpha = Denominator != 0.0 ? FMath::Min(1.0, (ResultsTime - End.StartTime) / Denominator) : 1.0;
            OutState.Interpolate(End, Alpha);

            return;
        }

        OutState = StateHistory.Last();
    }

    template <typename Traits>
    void USimState<Traits>::GetInterpolatedStateAtTimeSimProxy(Chaos::FReal ResultsTime, WrappedState& OutState) const {
        // The algorithm used by Chaos for interpolating states (mirrored in GetInterpolatedStateAtTime()) assumes that the previous state is the tick right before
        // the current tick. For simulated proxies this might not be the case, so we use a slightly different interpolation algorithm.
        if (StateHistory.IsEmpty()) {
            OutState = LastInterpolatedState;
            return;
        }

        for (int32 i = 0; i < StateHistory.Num() - 1; i++) {
            if (ResultsTime < StateHistory[i].StartTime || ResultsTime > StateHistory[i + 1].StartTime) { continue; }
            const WrappedState& Start = StateHistory[i];
            const WrappedState& End = StateHistory[i + 1];

            OutState = Start;

            const Chaos::FReal TimeFromStart = ResultsTime - Start.StartTime;
            const Chaos::FReal TotalTime = End.StartTime - Start.StartTime;

            if (TotalTime > 0.0) {
                const Chaos::FReal Alpha = FMath::Clamp(TimeFromStart / TotalTime, 0.0, 1.0);
                OutState.Interpolate(End, Alpha);
            }

            return;
        }

        OutState = StateHistory.Last();
    }

    template <typename Traits>
    Chaos::FRigidBodyHandle_Internal* USimState<Traits>::GetPhysHandle(const FNetTickInfo& TickInfo) {
        FBodyInstance* BodyInstance = TickInfo.UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) { return nullptr; }

        return BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
    }
}
