#pragma once

#include "CoreMinimal.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"

#include "ClientPredictionDelegate.h"
#include "ClientPredictionSimInput.h"
#include "ClientPredictionTick.h"

namespace ClientPrediction {
    struct USimCoordinatorBase {
    public:
        virtual ~USimCoordinatorBase() = default;
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) = 0;
        virtual void Destroy() = 0;
    };

    template <typename Traits>
    class USimCoordinator : public USimCoordinatorBase {
    public:
        explicit USimCoordinator(const TSharedPtr<USimInput<Traits>>& SimInput);
        virtual ~USimCoordinator() override = default;

    private:
        TSharedPtr<USimInput<Traits>> SimInput;

        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) override;
        virtual void Destroy() override;

    private:
        void InjectInputsGameThread(const int32 StartTick, const int32 NumTicks);
        void PreAdvance(const int32 TickNum);
        void PostAdvance(Chaos::FReal Dt);
        void OnPhysScenePostTick(FChaosScene* Scene);

        bool BuildTickInfo(int32 TickNum, FNetTickInfo& Info) const;

        FDelegateHandle InjectInputsGameThreadDelegate;
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
    USimCoordinator<Traits>::USimCoordinator(const TSharedPtr<ClientPrediction::USimInput<Traits>>& SimInput) : SimInput(SimInput) {
        SimInput->SetSimDelegates(SimDelegates);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewSimRole) {
        UpdatedComponent = NewUpdatedComponent;
        bHasNetConnection = bNowHasNetConnection;
        SimRole = NewSimRole;

        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        InjectInputsGameThreadDelegate = PhysCallback->InjectInputsExternal.AddRaw(this, &USimCoordinator::InjectInputsGameThread);
        PreAdvanceDelegate = PhysCallback->PreProcessInputsInternal.AddRaw(this, &USimCoordinator::PreAdvance);
        PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &USimCoordinator::PostAdvance));
        PhysScenePostTickDelegate = PhysScene->OnPhysScenePostTick.AddRaw(this, &USimCoordinator::OnPhysScenePostTick);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Destroy() {
        FPhysScene* PhysScene = GetPhysScene();
        if (PhysScene == nullptr) { return; }

        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        PhysCallback->InjectInputsExternal.Remove(InjectInputsGameThreadDelegate);
        PhysCallback->PreProcessInputsInternal.Remove(PreAdvanceDelegate);
        Solver->RemovePostAdvanceCallback(PostAdvanceDelegate);
        PhysScene->OnPhysScenePostTick.Remove(PhysScenePostTickDelegate);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::InjectInputsGameThread(const int32 StartTick, const int32 NumTicks) {
        if (SimInput == nullptr) { return; }

        for (int32 TickNum = StartTick; TickNum < StartTick + NumTicks; ++TickNum) {
            FNetTickInfo TickInfo{};
            if (!BuildTickInfo(TickNum, TickInfo)) {
                continue;
            }

            SimInput->InjectInputsGameThread(TickInfo);
        }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PreAdvance(const int32 TickNum) {
        if (SimInput == nullptr) { return; }

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickNum, TickInfo)) { return; }

        SimInput->PrepareInputPhysicsThread(TickInfo);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PostAdvance(Chaos::FReal Dt) {
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(Solver->GetCurrentFrame(), TickInfo)) { return; }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::OnPhysScenePostTick(FChaosScene* Scene) {
        if (SimInput == nullptr) { return; }
        SimInput->EmitInputs();
    }

    template <typename Traits>
    bool USimCoordinator<Traits>::BuildTickInfo(int32 TickNum, FNetTickInfo& Info) const {
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return false; }

        Info.bHasNetConnection = bHasNetConnection;
        Info.bIsResim = Solver->GetEvolution()->IsResimming();

        Info.Dt = Solver->GetAsyncDeltaTime();

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
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return nullptr; }

        Chaos::IRewindCallback* RewindCallback = Solver->GetRewindCallback();
        if (RewindCallback == nullptr) { return nullptr; }

        return static_cast<FNetworkPhysicsCallback*>(Solver->GetRewindCallback());
    }
}
