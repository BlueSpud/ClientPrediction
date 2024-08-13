#pragma once

#include "CoreMinimal.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionSimInput.h"
#include "ClientPredictionSimProxy.h"
#include "ClientPredictionSimState.h"
#include "ClientPredictionSimEvents.h"
#include "ClientPredictionUtils.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    class USimCoordinatorBase {
    public:
        virtual ~USimCoordinatorBase() = default;
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, ENetRole NewSimRole) = 0;
        virtual void Destroy() = 0;

        virtual void ConsumeInputBundle(FBundledPackets Packets) = 0;
        virtual void ConsumeSimProxyStates(FBundledPacketsLow Packets) = 0;
        virtual void ConsumeAutoProxyStates(FBundledPacketsFull Packets) = 0;
        virtual void ConsumeFinalState(FBundledPacketsFull Packets) = 0;

        virtual void ConsumeEvents(FBundledPackets Packets) = 0;
        virtual void ConsumeRemoteSimProxyOffset(FRemoteSimProxyOffset Offset) = 0;

        DECLARE_DELEGATE_OneParam(FRemoteSimProxyOffsetChangedDelegate, const FRemoteSimProxyOffset& Offset)
        FRemoteSimProxyOffsetChangedDelegate RemoteSimProxyOffsetChangedDelegate;
    };

    template <typename Traits>
    class USimCoordinator : public USimCoordinatorBase, public Chaos::ISimCallbackObject {
    public:
        explicit USimCoordinator(const TSharedPtr<USimInput<Traits>>& SimInput, const TSharedPtr<USimState<Traits>>& SimState,
                                 const TSharedPtr<USimEvents>& SimEvents);

        virtual ~USimCoordinator() override = default;

    private:
        TSharedPtr<USimInput<Traits>> SimInput;
        TSharedPtr<USimState<Traits>> SimState;
        TSharedPtr<USimEvents> SimEvents;

    public:
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, ENetRole NewSimRole) override;
        virtual void Destroy() override;

    private:
        void DestroyPT();
        void DestroyGT();

        virtual void FreeOutputData_External(Chaos::FSimCallbackOutput* Output) override {}
        virtual void FreeInputData_Internal(Chaos::FSimCallbackInput* Input) override {}

    private:
        virtual Chaos::FSimCallbackInput* AllocateInputData_External() override { return nullptr; }
        virtual void OnPreSimulate_Internal() override {}
        virtual int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedTick) override;

        void InjectInputsGT(const int32 StartTick, const int32 NumTicks);
        void PreAdvance(const int32 TickNum);
        void PostAdvance(Chaos::FReal Dt);
        void OnPhysScenePostTick(FChaosScene* Scene);

        bool BuildTickInfo(FNetTickInfo& Info) const;

        FDelegateHandle InjectInputsGTDelegateHandle;
        FDelegateHandle PreAdvanceDelegateHandle;
        FDelegateHandle PostAdvanceDelegateHandle;
        FDelegateHandle PhysScenePostTickDelegateHandle;
        FDelegateHandle RemoteSimProxyOffsetChangedDelegateHandle;

        TAtomic<ESimStage> SimStage = ESimStage::kRunning;
        TAtomic<bool> bDestroyedPT = false;
        TAtomic<bool> bDestroyedGT = false;

    public:
        virtual void ConsumeInputBundle(FBundledPackets Packets) override;
        virtual void ConsumeSimProxyStates(FBundledPacketsLow Packets) override;
        virtual void ConsumeAutoProxyStates(FBundledPacketsFull Packets) override;
        virtual void ConsumeFinalState(FBundledPacketsFull Packets) override;

        virtual void ConsumeEvents(FBundledPackets Packets) override;
        virtual void ConsumeRemoteSimProxyOffset(FRemoteSimProxyOffset Offset) override;

    private:
        UWorld* GetWorld() const;
        APlayerController* GetPlayerController() const;
        FPhysScene* GetPhysScene() const;
        Chaos::FPhysicsSolver* GetPhysSolver() const;
        FNetworkPhysicsCallback* GetPhysCallback() const;

    public:
        TSharedPtr<FSimDelegates<Traits>> GetSimDelegates() { return SimDelegates; };

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        ENetRole SimRole = ROLE_None;

        Chaos::FReal CachedSolverTime = -1.0;
        int32 CachedTickNumber = INDEX_NONE;
        int32 EarliestLocalTick = INDEX_NONE;
        Chaos::FReal LastResultsTime = -1.0;

        FCriticalSection FinalStateMutex;
        TOptional<FBundledPacketsFull> FinalStatePacket;
    };

    template <typename Traits>
    USimCoordinator<Traits>::USimCoordinator(const TSharedPtr<USimInput<Traits>>& SimInput, const TSharedPtr<USimState<Traits>>& SimState,
                                             const TSharedPtr<USimEvents>& SimEvents) :
        Chaos::ISimCallbackObject(Chaos::ESimCallbackOptions::Rewind), SimInput(SimInput), SimState(SimState), SimEvents(SimEvents),
        SimDelegates(MakeShared<FSimDelegates<Traits>>(SimEvents)) {
        SimInput->SetSimDelegates(SimDelegates);
        SimState->SetSimDelegates(SimDelegates);
        SimState->SetSimEvents(SimEvents);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Initialize(UPrimitiveComponent* NewUpdatedComponent, ENetRole NewSimRole) {
        if (SimInput == nullptr || SimState == nullptr) { return; }

        UpdatedComponent = NewUpdatedComponent;
        SimRole = NewSimRole;

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return; }

        Chaos::FRewindData* RewindData = PhysSolver->GetRewindData();
        if (RewindData == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        AClientPredictionSimProxyManager* SimProxyWorldManager = AClientPredictionSimProxyManager::ManagerForWorld(GetWorld());
        if (SimProxyWorldManager == nullptr) { return; }

        SimInput->SetBufferSize(RewindData->Capacity());
        SimState->SetBufferSize(RewindData->Capacity());
        SimEvents->SetHistoryDuration(RewindData->Capacity() * PhysSolver->GetAsyncDeltaTime());

        InjectInputsGTDelegateHandle = PhysCallback->InjectInputsExternal.AddRaw(this, &USimCoordinator::InjectInputsGT);
        PreAdvanceDelegateHandle = PhysCallback->PreProcessInputsInternal.AddRaw(this, &USimCoordinator::PreAdvance);
        PostAdvanceDelegateHandle = PhysSolver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &USimCoordinator::PostAdvance));
        PhysScenePostTickDelegateHandle = PhysScene->OnPhysScenePostTick.AddRaw(this, &USimCoordinator::OnPhysScenePostTick);
        RemoteSimProxyOffsetChangedDelegateHandle = SimProxyWorldManager->RemoteSimProxyOffsetChangedDelegate.AddLambda([this](const auto& Offset) {
            if (Offset.IsSet() && SimRole == ROLE_AutonomousProxy) { RemoteSimProxyOffsetChangedDelegate.ExecuteIfBound(Offset.GetValue()); }
        });

        PhysCallback->RegisterRewindableSimCallback_Internal(this);

        const TOptional<FRemoteSimProxyOffset>& RemoteSimProxyOffset = SimProxyWorldManager->GetRemoteSimProxyOffset();
        if (SimRole == ENetRole::ROLE_AutonomousProxy && RemoteSimProxyOffset.IsSet()) {
            RemoteSimProxyOffsetChangedDelegate.ExecuteIfBound(RemoteSimProxyOffset.GetValue());
        }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Destroy() {
        DestroyPT();
        DestroyGT();
    }

    template <typename Traits>
    void USimCoordinator<Traits>::DestroyPT() {
        if (bDestroyedPT.Exchange(true)) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        AClientPredictionSimProxyManager* SimProxyWorldManager = AClientPredictionSimProxyManager::ManagerForWorld(GetWorld());
        if (SimProxyWorldManager == nullptr) { return; }

        PhysCallback->InjectInputsExternal.Remove(InjectInputsGTDelegateHandle);
        PhysCallback->PreProcessInputsInternal.Remove(PreAdvanceDelegateHandle);
        PhysSolver->RemovePostAdvanceCallback(PostAdvanceDelegateHandle);

        PhysCallback->UnregisterRewindableSimCallback_Internal(this);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::DestroyGT() {
        if (bDestroyedGT.Exchange(true)) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->OnPhysScenePostTick.Remove(PhysScenePostTickDelegateHandle);
    }

    template <typename Traits>
    int32 USimCoordinator<Traits>::TriggerRewindIfNeeded_Internal(int32 LastCompletedTick) {
        if (UpdatedComponent == nullptr || SimState == nullptr || SimEvents == nullptr || SimRole != ROLE_AutonomousProxy) { return INDEX_NONE; }

        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return false; }

        const int32 RewindTick = SimState->GetRewindTick(PhysSolver, UpdatedComponent->GetPhysicsObjectByName(NAME_None));
        if (RewindTick != INDEX_NONE) {
            SimEvents->Rewind(RewindTick);
        }

        return RewindTick;
    }

    template <typename Traits>
    void USimCoordinator<Traits>::InjectInputsGT(const int32 StartTick, const int32 NumTicks) {
        if (SimInput != nullptr && SimStage == ESimStage::kRunning) {
            SimInput->InjectInputsGT();
        }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PreAdvance(const int32 TickNum) {
        if (SimInput == nullptr || SimState == nullptr || SimEvents == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return; }

        // Avoid simulating before the object was actually being simulated. This can happen if something rewinds physics before EarliestLocalTick
        EarliestLocalTick = EarliestLocalTick == INDEX_NONE ? TickNum : EarliestLocalTick;
        if (TickNum < EarliestLocalTick) { return; }

        CachedTickNumber = TickNum;
        CachedSolverTime = PhysSolver->GetSolverTime();

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickInfo)) { return; }

        if (SimRole != ROLE_Authority) {
            FScopeLock FinalStateLock(&FinalStateMutex);
            if (FinalStatePacket.IsSet()) {
                SimState->ConsumeFinalState(FinalStatePacket.GetValue(), TickInfo);
                FinalStatePacket.Reset();
            }
        }

        // State needs to come before the input because the input depends on the current state. If the simulation is over we don't need to prepare input anymore.
        SimStage = SimState->PreparePrePhysics(TickInfo);

        if (SimStage == ESimStage::kReadyForCleanup) {
            DestroyPT();
            return;
        }

        if (SimStage == ESimStage::kRunning) {
            SimInput->PreparePrePhysics(TickInfo, SimState->GetPrevState());
        }

        SimEvents->PreparePrePhysics(TickInfo);
        SimState->TickPrePhysics(TickInfo, SimInput->GetCurrentInput());
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PostAdvance(Chaos::FReal Dt) {
        if (SimInput == nullptr || SimState == nullptr) { return; }

        // Avoid simulating before the object was actually being simulated. This can happen if something rewinds physics before EarliestLocalTick
        if (EarliestLocalTick == INDEX_NONE || CachedTickNumber < EarliestLocalTick) {
            return;
        }

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickInfo)) { return; }

        SimState->TickPostPhysics(TickInfo, SimInput->GetCurrentInput());
    }

    template <typename Traits>
    void USimCoordinator<Traits>::OnPhysScenePostTick(FChaosScene* Scene) {
        if (SimInput == nullptr || SimState == nullptr || SimEvents == nullptr || EarliestLocalTick == INDEX_NONE) { return; }

        if (SimStage == ESimStage::kReadyForCleanup) {
            DestroyGT();
            return;
        }

        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return; }

        AClientPredictionSimProxyManager* SimProxyWorldManager = AClientPredictionSimProxyManager::ManagerForWorld(GetWorld());
        if (SimProxyWorldManager == nullptr) { return; }

        if (SimRole == ENetRole::ROLE_AutonomousProxy) {
            SimInput->EmitInputs();
        }

        if (SimRole == ENetRole::ROLE_Authority) {
            SimState->EmitStates();
            SimEvents->EmitEvents();
        }

        const Chaos::FReal ResultsTime = PhysSolver->GetPhysicsResultsTime_External();
        const Chaos::FReal SimProxyOffset = SimProxyWorldManager->GetLocalToServerOffset() * PhysSolver->GetAsyncDeltaTime();
        const Chaos::FReal Dt = LastResultsTime == -1.0 ? 0.0 : ResultsTime - LastResultsTime;

        SimState->InterpolateGameThread(UpdatedComponent, ResultsTime, SimProxyOffset, Dt, SimRole);
        SimEvents->ExecuteEvents(ResultsTime, SimProxyOffset, SimRole);

        LastResultsTime = ResultsTime;
    }

    template <typename Traits>
    bool USimCoordinator<Traits>::BuildTickInfo(FNetTickInfo& Info) const {
        if (UpdatedComponent == nullptr) { return false; }

        const UWorld* World = GetWorld();
        if (World == nullptr) { return false; }

        if (!FUtils::FillTickInfo(Info, CachedTickNumber, SimRole, World)) {
            return false;
        }

        Info.bHasNetConnection = UpdatedComponent->GetOwner() != nullptr && UpdatedComponent->GetOwner()->GetNetConnection() != nullptr;

        Info.StartTime = CachedSolverTime;
        Info.EndTime = Info.StartTime + Info.Dt;

        Info.UpdatedComponent = UpdatedComponent;
        Info.SimProxyWorldManager = AClientPredictionSimProxyManager::ManagerForWorld(World);
        Info.SimRole = SimRole;

        return true;
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeInputBundle(FBundledPackets Packets) {
        if (UpdatedComponent == nullptr || SimInput == nullptr) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->EnqueueAsyncPhysicsCommand(0, UpdatedComponent, [this, Packets = MoveTemp(Packets)]() {
            SimInput->ConsumeInputBundle(Packets);
        });
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeSimProxyStates(FBundledPacketsLow Packets) {
        if (UpdatedComponent == nullptr || SimState == nullptr || SimRole != ROLE_SimulatedProxy) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->EnqueueAsyncPhysicsCommand(0, UpdatedComponent, [this, Packets = MoveTemp(Packets)]() {
            if (Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver()) {
                SimState->ConsumeSimProxyStates(Packets, PhysSolver->GetAsyncDeltaTime());
            }
        });
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeAutoProxyStates(FBundledPacketsFull Packets) {
        if (UpdatedComponent == nullptr || SimState == nullptr || SimRole != ROLE_AutonomousProxy) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->EnqueueAsyncPhysicsCommand(0, UpdatedComponent, [this, Packets = MoveTemp(Packets)]() {
            SimState->ConsumeAutoProxyStates(Packets);
        });
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeFinalState(FBundledPacketsFull Packets) {
        FScopeLock FinalStateLock(&FinalStateMutex);
        FinalStatePacket = MoveTemp(Packets);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeEvents(FBundledPackets Packets) {
        if (UpdatedComponent == nullptr || SimEvents == nullptr || SimRole != ROLE_SimulatedProxy) { return; }

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->EnqueueAsyncPhysicsCommand(0, UpdatedComponent, [this, Packets = MoveTemp(Packets)]() {
            Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
            if (PhysSolver == nullptr) { return; }

            SimEvents->ConsumeEvents(Packets, PhysSolver->GetAsyncDeltaTime());
        });
    }

    template <typename Traits>
    void USimCoordinator<Traits>::ConsumeRemoteSimProxyOffset(FRemoteSimProxyOffset Offset) {
        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        PhysScene->EnqueueAsyncPhysicsCommand(0, UpdatedComponent, [this, Offset = MoveTemp(Offset)]() {
            SimEvents->ConsumeRemoteSimProxyOffset(Offset);
        });
    }

    template <typename Traits>
    UWorld* USimCoordinator<Traits>::GetWorld() const {
        if (UpdatedComponent == nullptr) { return nullptr; }
        return UpdatedComponent->GetWorld();
    }

    template <typename Traits>
    APlayerController* USimCoordinator<Traits>::GetPlayerController() const {
        UWorld* World = GetWorld();
        if (World == nullptr || !World->IsGameWorld()) { return nullptr; }

        return World->GetFirstPlayerController();
    }

    template <typename Traits>
    FPhysScene* USimCoordinator<Traits>::GetPhysScene() const {
        UWorld* World = GetWorld();
        if (World == nullptr || !World->IsGameWorld()) { return nullptr; }

        return World->GetPhysicsScene();
    }

    template <typename Traits>
    Chaos::FPhysicsSolver* USimCoordinator<Traits>::GetPhysSolver() const {
        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return nullptr; }

        return PhysScene->GetSolver();
    }

    template <typename Traits>
    FNetworkPhysicsCallback* USimCoordinator<Traits>::GetPhysCallback() const {
        Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver();
        if (PhysSolver == nullptr) { return nullptr; }

        Chaos::IRewindCallback* RewindCallback = PhysSolver->GetRewindCallback();
        if (RewindCallback == nullptr) { return nullptr; }

        return static_cast<FNetworkPhysicsCallback*>(PhysSolver->GetRewindCallback());
    }
}
