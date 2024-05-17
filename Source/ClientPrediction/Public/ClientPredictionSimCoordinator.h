#pragma once

#include "CoreMinimal.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"

namespace ClientPrediction {
    struct FClientPredictionInputBuf {};

    struct FNetTickInfo {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        bool bIsResim = false;
        Chaos::FReal Dt = 0.0;
    };

    class USimCoordinatorBase {
    public:
        virtual ~USimCoordinatorBase() = default;
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewRole) = 0;
        virtual void Destroy() = 0;
    };

    template <typename Traits>
    class USimCoordinator : public USimCoordinatorBase {
    public:
        virtual ~USimCoordinator() override = default;
        virtual void Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewRole) override;
        virtual void Destroy() override;

    private:
        void PreAdvance(const int32 PhysicsStep);
        void PostAdvance(Chaos::FReal Dt);

        bool BuildTickInfo(FNetTickInfo& Info) const;
        bool ShouldProduceInputForTick(FNetTickInfo& Info) const;

        FDelegateHandle PreAdvanceDelegate;
        FDelegateHandle PostAdvanceDelegate;

        UWorld* GetWorld() const;
        APlayerController* GetPlayerController() const;
        FPhysScene* GetPhysScene() const;
        Chaos::FPhysicsSolver* GetSolver() const;
        FNetworkPhysicsCallback* GetPhysCallback() const;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        bool bHasNetConnection = false;
        ENetRole Role = ROLE_None;
    };

    template <typename Traits>
    void USimCoordinator<Traits>::Initialize(UPrimitiveComponent* NewUpdatedComponent, bool bNowHasNetConnection, ENetRole NewRole) {
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        PreAdvanceDelegate = PhysCallback->PreProcessInputsInternal.AddRaw(this, &USimCoordinator::PreAdvance);
        PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &USimCoordinator::PostAdvance));

        UpdatedComponent = NewUpdatedComponent;
        bHasNetConnection = bNowHasNetConnection;
        Role = NewRole;
    }

    template <typename Traits>
    void USimCoordinator<Traits>::Destroy() {
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        PhysCallback->PreProcessInputsInternal.Remove(PreAdvanceDelegate);
        Solver->RemovePostAdvanceCallback(PostAdvanceDelegate);
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PreAdvance(const int32 PhysicsStep) {
        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickInfo)) { return; }
    }

    template <typename Traits>
    void USimCoordinator<Traits>::PostAdvance(Chaos::FReal Dt) {
        FNetTickInfo TickInfo{};
        if (!BuildTickInfo(TickInfo)) { return; }
    }

    template <typename Traits>
    bool USimCoordinator<Traits>::BuildTickInfo(FNetTickInfo& Info) const {
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return false; }

        Chaos::FRewindData* RewindData = Solver->GetRewindData();
        if (RewindData == nullptr) { return false; }

        Info.bIsResim = Solver->GetEvolution()->IsResimming();
        Info.Dt = Solver->GetAsyncDeltaTime();

        if (Role != ROLE_Authority) {
            if (APlayerController* PC = GetPlayerController()) {
                FAsyncPhysicsTimestamp SyncedTimestamp = PC->GetPhysicsTimestamp();
                Info.LocalTick = SyncedTimestamp.LocalFrame;
                Info.ServerTick = SyncedTimestamp.ServerFrame;
            }
            else { return false; }
        }
        else {
            Info.LocalTick = RewindData->CurrentFrame();
            Info.ServerTick = RewindData->CurrentFrame();
        }

        return true;
    }

    template <typename Traits>
    bool USimCoordinator<Traits>::ShouldProduceInputForTick(FNetTickInfo& Info) const {
        return Role == ROLE_AutonomousProxy || (Role == ROLE_Authority && !bHasNetConnection);
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
