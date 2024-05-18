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
        void PreAdvance(const int32 PhysicsStep);
        void PostAdvance(Chaos::FReal Dt);

        bool BuildTickInfo(FNetTickInfo& Info) const;

        FDelegateHandle PreAdvanceDelegate;
        FDelegateHandle PostAdvanceDelegate;

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
        Chaos::FPhysicsSolver* Solver = GetSolver();
        if (Solver == nullptr) { return; }

        FNetworkPhysicsCallback* PhysCallback = GetPhysCallback();
        if (PhysCallback == nullptr) { return; }

        PreAdvanceDelegate = PhysCallback->PreProcessInputsInternal.AddRaw(this, &USimCoordinator::PreAdvance);
        PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &USimCoordinator::PostAdvance));

        UpdatedComponent = NewUpdatedComponent;
        bHasNetConnection = bNowHasNetConnection;
        SimRole = NewSimRole;
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

        Info.bHasNetConnection = bHasNetConnection;
        Info.bIsResim = Solver->GetEvolution()->IsResimming();

        Info.Dt = Solver->GetAsyncDeltaTime();

        Info.UpdatedComponent = UpdatedComponent;
        Info.SimRole = SimRole;

        if (SimRole != ROLE_Authority) {
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
