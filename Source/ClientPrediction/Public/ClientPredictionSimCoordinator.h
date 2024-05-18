﻿#pragma once

#include "CoreMinimal.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionSimInput.h"
#include "ClientPredictionSimState.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    struct USimCoordinatorBase {
    public:
        virtual ~USimCoordinatorBase() = default;
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) = 0;
        virtual void Destroy() = 0;
    };

    template <typename Traits>
    class USimCoordinator : public USimCoordinatorBase, public Chaos::ISimCallbackObject {
    public:
        explicit USimCoordinator(const TSharedPtr<USimInput<Traits>>& SimInput, const TSharedPtr<USimState<Traits>>& SimState);
        virtual ~USimCoordinator() override = default;

    private:
        TSharedPtr<USimInput<Traits>> SimInput;
        TSharedPtr<USimState<Traits>> SimState;

        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) override;
        virtual void Destroy() override;

    public:
        virtual void FreeOutputData_External(Chaos::FSimCallbackOutput* Output) override {}
        virtual void FreeInputData_Internal(Chaos::FSimCallbackInput* Input) override {}

    private:
        virtual Chaos::FSimCallbackInput* AllocateInputData_External() override { return nullptr; }
        virtual void OnPreSimulate_Internal() override {}
        virtual int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) override;

        void InjectInputsGT(const int32 StartTick, const int32 NumTicks);
        void PreAdvance(const int32 TickNum);
        void PostAdvance(Chaos::FReal Dt);
        void OnPhysScenePostTick(FChaosScene* Scene);

        bool BuildTickInfo(int32 TickNum, FNetTickInfo& Info) const;

        FDelegateHandle InjectInputsGTDelegate;
        FDelegateHandle PreAdvanceDelegate;
        FDelegateHandle PostAdvanceDelegate;
        FDelegateHandle PhysScenePostTickDelegate;

        UWorld* GetWorld() const;
        APlayerController* GetPlayerController() const;
        FPhysScene* GetPhysScene() const;
        Chaos::FPhysicsSolver* GetSolver() const;
        FNetworkPhysicsCallback* GetPhysCallback() const;

    public:
        TSharedPtr<FSimDelegates<Traits>> GetSimDelegates() { return SimDelegates; };

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates = MakeShared<FSimDelegates<Traits>>();

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        bool bHasNetConnection = false;
        ENetRole SimRole = ROLE_None;
    };

    template <typename Traits>
    USimCoordinator<Traits>::USimCoordinator(const TSharedPtr<USimInput<Traits>>& SimInput, const TSharedPtr<USimState<Traits>>& SimState) :
        Chaos::ISimCallbackObject(Chaos::ESimCallbackOptions::Rewind), SimInput(SimInput), SimState(SimState) {
        SimInput->SetSimDelegates(SimDelegates);
        SimState->SetSimDelegates(SimDelegates);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) {
        UpdatedComponent = NewUpdatedComponent;
        bHasNetConnection = bNowHasNetConnection;
        SimRole = NewSimRole;

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = GetSolver();
        if (PhysSolver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        InjectInputsGTDelegate = PhysCallback->InjectInputsExternal.AddRaw(this, &USimCoordinator::InjectInputsGT);
        PreAdvanceDelegate = PhysCallback->PreProcessInputsInternal.AddRaw(this, &USimCoordinator::PreAdvance);
        PostAdvanceDelegate = PhysSolver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &USimCoordinator::PostAdvance));
        PhysScenePostTickDelegate = PhysScene->OnPhysScenePostTick.AddRaw(this, &USimCoordinator::OnPhysScenePostTick);

        PhysCallback->RegisterRewindableSimCallback_Internal(this);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Destroy() {
        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* PhysSolver = GetSolver();
        if (PhysSolver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        PhysCallback->InjectInputsExternal.Remove(InjectInputsGTDelegate);
        PhysCallback->PreProcessInputsInternal.Remove(PreAdvanceDelegate);
        PhysSolver->RemovePostAdvanceCallback(PostAdvanceDelegate);
        PhysScene->OnPhysScenePostTick.Remove(PhysScenePostTickDelegate);

        PhysCallback->UnregisterRewindableSimCallback_Internal(this);
    }

    template <typename Traits>
    int32 USimCoordinator<Traits>::TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) {
        if (SimState == nullptr) { return INDEX_NONE; }
        return INDEX_NONE;
    }

    template <typename Traits>
    void USimCoordinator<Traits>::InjectInputsGT(const int32 StartTick, const int32 NumTicks) {
        if (SimInput == nullptr) { return; }

        for (int32 TickNum = StartTick; TickNum < StartTick + NumTicks; ++TickNum) {
            FNetTickInfo TickInfo{};
            if (!BuildTickInfo(TickNum, TickInfo)) {
                continue;
            }

            SimInput->InjectInputsGT(TickInfo);
        }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PreAdvance(const int32 TickNum) {
        if (SimInput == nullptr) { return; }

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickNum, TickInfo)) { return; }

        SimInput->PrepareInputPhysicsThread(TickInfo);
        SimState->TickPrePhysics(TickInfo, SimInput->GetCurrentInput());
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PostAdvance(Chaos::FReal Dt) {
        Chaos::FPhysicsSolver* PhysSolver = GetSolver();
        if (PhysSolver == nullptr) { return; }

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(PhysSolver->GetCurrentFrame(), TickInfo)) { return; }

        SimState->TickPostPhysics(TickInfo, SimInput->GetCurrentInput());
    }

    template <typename Traits>
    void USimCoordinator<Traits>::OnPhysScenePostTick(FChaosScene* Scene) {
        if (SimInput == nullptr) { return; }
        SimInput->EmitInputs();
    }

    template <typename Traits>
    bool USimCoordinator<Traits>::BuildTickInfo(int32 TickNum, FNetTickInfo& Info) const {
        Chaos::FPhysicsSolver* PhysSolver = GetSolver();
        if (PhysSolver == nullptr) { return false; }

        Info.bHasNetConnection = bHasNetConnection;
        Info.bIsResim = PhysSolver->GetEvolution()->IsResimming();

        Info.Dt = PhysSolver->GetAsyncDeltaTime();
        Info.StartTime = PhysSolver->GetSolverTime();
        Info.EndTime = Info.StartTime + Info.Dt;

        Info.UpdatedComponent = UpdatedComponent;
        Info.SimRole = SimRole;

        if (SimRole != ENetRole::ROLE_Authority) {
            if (APlayerController* PC = GetPlayerController()) {
                Info.LocalTick = TickNum;
                Info.ServerTick = TickNum + PC->GetNetworkPhysicsTickOffset();
            }
            else { return false; }
        }
        else {
            Info.LocalTick = TickNum;
            Info.ServerTick = TickNum;
        }

        return true;
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
    Chaos::FPhysicsSolver* USimCoordinator<Traits>::GetSolver() const {
        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return nullptr; }

        return PhysScene->GetSolver();
    }

    template <typename Traits>
    FNetworkPhysicsCallback* USimCoordinator<Traits>::GetPhysCallback() const {
        Chaos::FPhysicsSolver* PhysSolver = GetSolver();
        if (PhysSolver == nullptr) { return nullptr; }

        Chaos::IRewindCallback* RewindCallback = PhysSolver->GetRewindCallback();
        if (RewindCallback == nullptr) { return nullptr; }

        return static_cast<FNetworkPhysicsCallback*>(PhysSolver->GetRewindCallback());
    }
}
