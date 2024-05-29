#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Chaos/PhysicsObjectInternalInterface.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionSimEvents.h"
#include "ClientPredictionTick.h"
#include "ClientPredictionPhysState.h"
#include "ClientPredictionCVars.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FWrappedState {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;
        bool bIsFinalState = false;

        StateType State{};
        FPhysState PhysState{};

        // These are not sent over the network, they're used for local interpolation only
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        void NetSerialize(FArchive& Ar, EDataCompleteness Completeness, void* Userdata);
        void Interpolate(const FWrappedState& Other, Chaos::FReal Alpha);
        void Extrapolate(const FWrappedState& PrevState, Chaos::FReal ExtrapolationTime);
    };

    template <typename StateType>
    void FWrappedState<StateType>::NetSerialize(FArchive& Ar, EDataCompleteness Completeness, void* Userdata) {
        if (Ar.IsSaving()) {
            checkSlow(ServerTick >= INDEX_NONE);

            uint32 Packed = (ServerTick + 1) | (bIsFinalState << 31);
            Ar << Packed;
        }
        else {
            uint32 Packed;
            Ar << Packed;

            ServerTick = static_cast<int32>(Packed & ~0x80000000) - 1;
            bIsFinalState = static_cast<bool>(Packed >> 31);
        }

        PhysState.NetSerialize(Ar, Completeness);
        State.NetSerialize(Ar, Completeness);
    }

    template <typename StateType>
    void FWrappedState<StateType>::Interpolate(const FWrappedState& Other, Chaos::FReal Alpha) {
        PhysState.Interpolate(Other.PhysState, Alpha);
        State.Interpolate(Other.State, Alpha);
    }

    template <typename StateType>
    void FWrappedState<StateType>::Extrapolate(const FWrappedState& PrevState, Chaos::FReal ExtrapolationTime) {
        const Chaos::FReal StateDt = EndTime - PrevState.EndTime;
        PhysState.Extrapolate(PrevState.PhysState, StateDt, ExtrapolationTime);
    }

    class USimStateBase {
    public:
        virtual ~USimStateBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitLowStateDelegate, const FBundledPacketsLow& Bundle)
        FEmitLowStateDelegate EmitSimProxyBundle;

        DECLARE_DELEGATE_OneParam(FEmitFullStateDelegate, const FBundledPacketsFull& Bundle)
        FEmitFullStateDelegate EmitAutoProxyBundle;
        FEmitFullStateDelegate EmitFinalBundle;
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
        void SetSimEvents(const TSharedPtr<USimEvents>& NewSimEvents);
        void SetBufferSize(int32 BufferSize);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;
        TSharedPtr<USimEvents> SimEvents;

    public:
        int32 ConsumeSimProxyStates(const FBundledPacketsLow& Packets, Chaos::FReal SimDt);
        void ConsumeAutoProxyStates(const FBundledPacketsFull& Packets);
        void ConsumeFinalState(const FBundledPacketsFull& Packets, const FNetTickInfo& TickInfo);

    private:
        void UpdateTimesRecvSimProxy(WrappedState& State, Chaos::FReal SimDt);

    private:
        static void FillStateSimDetails(WrappedState& State, const FNetTickInfo& TickInfo);
        static void FillStatePhysInfo(WrappedState& State, const FNetTickInfo& TickInfo);
        void GenerateInitialState(const FNetTickInfo& TickInfo);

    public:
        void PreparePrePhysics(const FNetTickInfo& TickInfo);

    private:
        void TrimStateBuffer();

    public:
        void TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input);
        void TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input);

    private:
        void UpdateStateHistory(const FNetTickInfo& TickInfo, const WrappedState& State);

        bool IsSimOverPT(const FNetTickInfo& TickInfo);
        void EndSimIfNeeded(const FNetTickInfo& TickInfo);
        void EndSimPT(const FNetTickInfo& TickInfo);

    public:
        int32 GetRewindTick(Chaos::FPhysicsSolver* PhysSolver, Chaos::FPhysicsObjectHandle PhysObject);
        void ApplyCorrectionIfNeeded(const FNetTickInfo& TickInfo);

        void EmitStates();
        void InterpolateGameThread(UPrimitiveComponent* UpdatedComponent, Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, Chaos::FReal Dt, ENetRole SimRole);

    private:
        void GetInterpolatedStateAtTime(Chaos::FReal ResultsTime, WrappedState& OutState);
        static Chaos::FRigidBodyHandle_Internal* GetPhysHandle(const FNetTickInfo& TickInfo);

    public:
        const StateType& GetPrevState() { return PrevState.State; }

    private:
        FCriticalSection StateMutex;
        TArray<WrappedState> StateHistory;
        int32 StateHistoryCapacity = INDEX_NONE;

        WrappedState PrevState{};
        WrappedState CurrentState{};
        WrappedState LastInterpolatedState{};
        bool bGeneratedInitialState = false;
        bool bEndedSimOnGameThread = false;

        FCriticalSection FinalStateMutex;
        WrappedState FinalState{};

        // Relevant only for sim proxies
        ECollisionEnabled::Type CachedCollisionMode = ECollisionEnabled::NoCollision;

        // Relevant only for auto proxies
        WrappedState LatestAuthorityState{};
        int32 LatestAckedServerTick = INDEX_NONE;

        TOptional<WrappedState> PendingCorrection;
        bool bAutoProxyAppliedFinalState = false;

        // Relevant only for the authorities
        int32 LatestEmittedTick = INDEX_NONE;
    };

    template <typename Traits>
    void USimState<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimState<Traits>::SetSimEvents(const TSharedPtr<USimEvents>& NewSimEvents) {
        SimEvents = NewSimEvents;
    }

    template <typename Traits>
    void USimState<Traits>::SetBufferSize(int32 BufferSize) {
        StateHistoryCapacity = BufferSize;
    }

    template <typename Traits>
    int32 USimState<Traits>::ConsumeSimProxyStates(const FBundledPacketsLow& Packets, Chaos::FReal SimDt) {
        FScopeLock StateLock(&StateMutex);

        TArray<WrappedState> AuthorityStates;
        Packets.Bundle().Retrieve(AuthorityStates, this);

        // We add the states to a queue so that we can update these states as well as the existing ones in SimProxyAdjustStateTimeline(). We can't do all that
        // logic in this function since the sim proxy offset might change in between receives.
        int32 NewestState = INDEX_NONE;
        for (WrappedState& NewState : AuthorityStates) {
            NewestState = FMath::Max(NewestState, NewState.ServerTick);

            if (StateHistory.ContainsByPredicate([&](const WrappedState& Other) { return Other.ServerTick == NewState.ServerTick; })) {
                continue;
            }

            UpdateTimesRecvSimProxy(NewState, SimDt);
            StateHistory.Add(MoveTemp(NewState));
        }

        StateHistory.Sort([](const WrappedState& Lhs, const WrappedState& Rhs) { return Lhs.ServerTick < Rhs.ServerTick; });

        return NewestState;
    }

    template <typename Traits>
    void USimState<Traits>::ConsumeAutoProxyStates(const FBundledPacketsFull& Packets) {
        TArray<WrappedState> AuthorityStates;
        Packets.Bundle().Retrieve(AuthorityStates, this);

        if (AuthorityStates.IsEmpty() || AuthorityStates.Last().ServerTick <= LatestAuthorityState.ServerTick) { return; }
        LatestAuthorityState = AuthorityStates.Last();
    }

    template <typename Traits>
    void USimState<Traits>::ConsumeFinalState(const FBundledPacketsFull& Packets, const FNetTickInfo& TickInfo) {
        FScopeLock FinalStateLock(&FinalStateMutex);
        FScopeLock StateLock(&StateMutex);

        TArray<WrappedState> AuthorityState;
        Packets.Bundle().Retrieve(AuthorityState, this);

        check(AuthorityState.Num() == 1);
        FinalState = AuthorityState[0];

        if (TickInfo.SimRole == ROLE_SimulatedProxy) {
            UpdateTimesRecvSimProxy(FinalState, TickInfo.Dt);

            // The final state should always be the last. Plus we sort in ConsumeSimProxyStates() so it shouldn't be a problem if another state is received after.
            StateHistory.Add(FinalState);
            return;
        }

        // We use the current tick offset between the client and server to assign a local tick. This way, if the offset changes we still have a fixed point in time
        // that the simulation will end on the client. If it does change, this simulation doesn't really care because it's already over on the authority. This offset
        // will most likely be negative since the client predicts ahead of the authority.
        FinalState.LocalTick = TickInfo.LocalTick + (FinalState.ServerTick - TickInfo.ServerTick);
    }

    template <typename Traits>
    void USimState<Traits>::UpdateTimesRecvSimProxy(WrappedState& State, Chaos::FReal SimDt) {
        // By keeping the times in server time we avoid needing maintenance on the buffer if the offset changes.
        State.StartTime = static_cast<Chaos::FReal>(State.ServerTick) * SimDt;
        State.EndTime = State.StartTime + SimDt;
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
        USimState::FillStatePhysInfo(CurrentState, TickInfo);
        SimDelegates->GenerateInitialStatePTDelegate.Broadcast(CurrentState.State);

        StateHistory.Add(CurrentState);
    }

    template <typename Traits>
    void USimState<Traits>::PreparePrePhysics(const FNetTickInfo& TickInfo) {
        if (SimDelegates == nullptr) { return; }

        if (TickInfo.SimRole == ROLE_SimulatedProxy) {
            return;
        }

        // We need to unlock the state mutex before checking if the sim is over to prevent deadlock since everything is done final state then state lock.
        {
            FScopeLock StateLock(&StateMutex);
            TrimStateBuffer();
        }

        if (IsSimOverPT(TickInfo)) {
            return;
        }

        FScopeLock StateLock(&StateMutex);
        if (!bGeneratedInitialState) {
            GenerateInitialState(TickInfo);
            bGeneratedInitialState = true;
        }

        ApplyCorrectionIfNeeded(TickInfo);

        const int32 PrevTickNumber = TickInfo.LocalTick - 1;
        for (const WrappedState& State : StateHistory) {
            if (State.LocalTick > PrevTickNumber) { break; }

            PrevState = State;
            if (State.LocalTick == PrevTickNumber) { break; }
        }
    }

    template <typename Traits>
    void USimState<Traits>::TrimStateBuffer() {
        if (StateHistoryCapacity == INDEX_NONE) { return; }

        while (StateHistory.Num() > StateHistoryCapacity) {
            StateHistory.RemoveAt(0, EAllowShrinking::No);
        }
    }

    template <typename Traits>
    void USimState<Traits>::TickPrePhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (SimDelegates == nullptr || TickInfo.SimRole == ROLE_SimulatedProxy) { return; }

        EndSimIfNeeded(TickInfo);
        if (IsSimOverPT(TickInfo)) {
            return;
        }

        CurrentState.State = PrevState.State;

        FTickOutput Output(CurrentState.State, TickInfo, SimEvents);
        SimDelegates->SimTickPrePhysicsDelegate.Broadcast(TickInfo, Input, PrevState.State, Output);
    }

    template <typename Traits>
    void USimState<Traits>::TickPostPhysics(const FNetTickInfo& TickInfo, const InputType& Input) {
        if (SimDelegates == nullptr || TickInfo.SimRole == ROLE_SimulatedProxy) { return; }

        if (IsSimOverPT(TickInfo)) {
            return;
        }

        FTickOutput Output(CurrentState.State, TickInfo, SimEvents);
        SimDelegates->SimTickPostPhysicsDelegate.Broadcast(TickInfo, Input, PrevState.State, Output);
        USimState::FillStateSimDetails(CurrentState, TickInfo);

        UpdateStateHistory(TickInfo, CurrentState);
    }

    template <typename Traits>
    void USimState<Traits>::UpdateStateHistory(const FNetTickInfo& TickInfo, const WrappedState& State) {
        FScopeLock StateLock(&StateMutex);
        if (StateHistory.IsEmpty() || StateHistory.Last().LocalTick < TickInfo.LocalTick) {
            StateHistory.Add(State);
            return;
        }

        for (int32 StateIdx = 0; StateIdx < StateHistory.Num(); ++StateIdx) {
            WrappedState& HistoricState = StateHistory[StateIdx];
            if (HistoricState.LocalTick != TickInfo.LocalTick) {
                continue;
            }

            HistoricState = State;
            if (!State.bIsFinalState) {
                return;
            }

            // Auto proxies were probably ahead of the authority, so there were most likely states that were predicted after the end of the simulation.
            // These states never actually happened on the authority , so we want to remove them.
            const int32 NextStateIdx = StateIdx + 1;
            while (StateHistory.Num() > NextStateIdx) {
                StateHistory.RemoveAt(NextStateIdx);
            }

            return;
        }
    }

    template <typename Traits>
    bool USimState<Traits>::IsSimOverPT(const FNetTickInfo& TickInfo) {
        // Auto proxies assign a local tick when the final state is consumed to avoid a changing server offset causing the simulation to report as not over for a few ticks.
        // Authorities can just use the local tick because for them LocalTick == ServerTick.
        FScopeLock FinalStateLock(&FinalStateMutex);
        return FinalState.LocalTick != INDEX_NONE && TickInfo.LocalTick >= FinalState.LocalTick;
    }

    template <typename Traits>
    void USimState<Traits>::EndSimIfNeeded(const FNetTickInfo& TickInfo) {
        FScopeLock FinalStateLock(&FinalStateMutex);

        if (TickInfo.SimRole == ROLE_AutonomousProxy && FinalState.LocalTick != INDEX_NONE && TickInfo.LocalTick >= FinalState.LocalTick) {
            if (bAutoProxyAppliedFinalState) { return; }
            bAutoProxyAppliedFinalState = true;

            // Now that we know when the final state is actually applied, we can update all of the relevant values on it. 
            FinalState.LocalTick = TickInfo.LocalTick;
            FinalState.ServerTick = TickInfo.ServerTick;

            FinalState.StartTime = TickInfo.StartTime;
            FinalState.EndTime = TickInfo.EndTime;

            EndSimPT(TickInfo);
            UpdateStateHistory(TickInfo, FinalState);

            return;
        }

        // We only allow the authority to end the simulation so that the client doesn't mispredict and end it early.
        if (TickInfo.SimRole != ROLE_Authority || FinalState.ServerTick != INDEX_NONE) {
            return;
        }

        if (!SimDelegates->IsSimFinishedDelegate.IsBound() || !SimDelegates->IsSimFinishedDelegate.Execute(TickInfo, PrevState.State)) {
            return;
        }

        FinalState.bIsFinalState = true;
        FinalState.State = PrevState.State;
        USimState::FillStateSimDetails(FinalState, TickInfo);

        UpdateStateHistory(TickInfo, FinalState);
        EndSimPT(TickInfo);
    }

    template <typename Traits>
    void USimState<Traits>::EndSimPT(const FNetTickInfo& TickInfo) {
        Chaos::FRigidBodyHandle_Internal* Handle = GetPhysHandle(TickInfo);
        if (Handle == nullptr) { return; }

        Handle->SetX(FinalState.PhysState.X);
        Handle->SetR(FinalState.PhysState.R);

        Handle->SetV(Chaos::FVec3(0.0, 0.0, 0.0));
        Handle->SetW(Chaos::FVec3(0.0, 0.0, 0.0));

        Handle->SetObjectState(Chaos::EObjectStateType::Static);
    }

    template <typename Traits>
    int32 USimState<Traits>::GetRewindTick(Chaos::FPhysicsSolver* PhysSolver, Chaos::FPhysicsObjectHandle PhysObject) {
        if (LatestAuthorityState.ServerTick == INDEX_NONE || LatestAuthorityState.ServerTick <= LatestAckedServerTick) { return INDEX_NONE; }
        LatestAckedServerTick = LatestAuthorityState.ServerTick;

        Chaos::FRewindData* RewindData = PhysSolver->GetRewindData();
        if (RewindData == nullptr) { return INDEX_NONE; }

        FScopeLock StateLock(&StateMutex);
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

    template <typename Traits>
    void USimState<Traits>::EmitStates() {
        FScopeLock FinalStateLock(&FinalStateMutex);
        FScopeLock StateLock(&StateMutex);

        if (StateHistory.IsEmpty() || StateHistory.Last().ServerTick <= LatestEmittedTick) {
            return;
        }

        if (FinalState.ServerTick != INDEX_NONE) {
            FBundledPacketsFull FinalStatePacket{};
            TArray<WrappedState> FinalStateArr = {FinalState};

            FinalStatePacket.Bundle().Store(FinalStateArr, this);
            EmitFinalBundle.ExecuteIfBound(FinalStatePacket);

            LatestEmittedTick = TNumericLimits<int32>::Max();
            return;
        }

        FBundledPacketsFull AutoProxyPackets{};
        TArray<WrappedState> AutoProxyStates = {StateHistory.Last()};

        AutoProxyPackets.Bundle().Store(AutoProxyStates, this);
        EmitAutoProxyBundle.ExecuteIfBound(AutoProxyPackets);

        TArray<WrappedState> SimProxyStates;
        for (const WrappedState& State : StateHistory) {
            if (State.ServerTick > LatestEmittedTick && State.ServerTick % ClientPredictionSimProxySendInterval == 0) {
                SimProxyStates.Add(State);
            }
        }

        if (!SimProxyStates.IsEmpty()) {
            FBundledPacketsLow SimProxyPackets{};
            SimProxyPackets.Bundle().Store(SimProxyStates, this);
            EmitSimProxyBundle.ExecuteIfBound(SimProxyPackets);
        }

        LatestEmittedTick = StateHistory.Last().ServerTick;
    }

    template <typename Traits>
    void USimState<Traits>::InterpolateGameThread(UPrimitiveComponent* UpdatedComponent, Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, Chaos::FReal Dt,
                                                  ENetRole SimRole) {
        if (UpdatedComponent == nullptr || SimDelegates == nullptr || bEndedSimOnGameThread) { return; }

        Chaos::FReal AdjustedResultsTime = SimRole != ROLE_SimulatedProxy ? ResultsTime : ResultsTime + SimProxyOffset;
        GetInterpolatedStateAtTime(AdjustedResultsTime, LastInterpolatedState);

        FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) { return; }

        // Sim proxies have custom logic since they aren't really simulated.
        if (SimRole == ROLE_SimulatedProxy) {
            Chaos::FRigidBodyHandle_External& Handle = BodyInstance->GetPhysicsActorHandle()->GetGameThreadAPI();
            Handle.SetObjectState(Chaos::EObjectStateType::Kinematic);

            const ECollisionEnabled::Type CurrentCollisionMode = UpdatedComponent->GetCollisionEnabled();
            if (CurrentCollisionMode != ECollisionEnabled::QueryAndProbe) {
                CachedCollisionMode = CurrentCollisionMode;
            }

            UpdatedComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndProbe);

            Handle.SetX(LastInterpolatedState.PhysState.X);
            Handle.SetR(LastInterpolatedState.PhysState.R);
        }

        SimDelegates->FinalizeDelegate.Broadcast(LastInterpolatedState.State, Dt);

        if (!LastInterpolatedState.bIsFinalState) {
            return;
        }

        if (SimRole == ROLE_SimulatedProxy) {
            UpdatedComponent->SetCollisionEnabled(CachedCollisionMode);
        }

        UpdatedComponent->SyncComponentToRBPhysics();
        BodyInstance->SetInstanceSimulatePhysics(false, true, true);

        bEndedSimOnGameThread = true;
    }

    template <typename Traits>
    void USimState<Traits>::GetInterpolatedStateAtTime(Chaos::FReal ResultsTime, WrappedState& OutState) {
        FScopeLock StateLock(&StateMutex);

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

            // This mostly mirrors the Chaos interpolation algorithm except we use the end time of the start state, rather than the end time of the end state.
            // This is because for sim proxies the state buffer might not have every tick in it and this will handle it more gracefully.
            const Chaos::FReal Denominator = End.EndTime - Start.EndTime;
            const Chaos::FReal Alpha = Denominator != 0.0 ? FMath::Min(1.0, (ResultsTime - Start.EndTime) / Denominator) : 1.0;
            OutState.Interpolate(End, Alpha);

            return;
        }

        OutState = StateHistory.Last();

        if (StateHistory.Num() == 1 || OutState.bIsFinalState) {
            return;
        }

        const Chaos::FReal ExtrapolationTime = ResultsTime - OutState.EndTime;
        OutState.Extrapolate(StateHistory[StateHistory.Num() - 2], ExtrapolationTime);
    }

    template <typename Traits>
    Chaos::FRigidBodyHandle_Internal* USimState<Traits>::GetPhysHandle(const FNetTickInfo& TickInfo) {
        FBodyInstance* BodyInstance = TickInfo.UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) { return nullptr; }

        return BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
    }
}
