#pragma once

#include "World/ClientPredictionWorldManager.h"
#include "World/ClientPredictionTickCallback.h"
#include "World/ClientPredictionReplicationManager.h"

#include "PBDRigidsSolver.h"
#include "Physics/PhysicsInterfaceScene.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace ClientPrediction {
    TMap<UWorld*, FWorldManager*> FWorldManager::Managers;

    // Initialization

    FWorldManager* FWorldManager::InitializeWorld(UWorld* World) {
        check(!Managers.Contains(World));

        FWorldManager* Manager = new FWorldManager(World);
        Managers.Add(World, Manager);

        return Manager;
    }

    FWorldManager* FWorldManager::ManagerForWorld(const UWorld* World) {
        if (!Managers.Contains(World)) { return nullptr; }
        return Managers[World];
    }

    void FWorldManager::CleanupWorld(const UWorld* World) {
        if (!Managers.Contains(World)) { return; }

        const FWorldManager* WorldManager = Managers[World];
        Managers.Remove(World);

        delete WorldManager;
    }

    FWorldManager::FWorldManager(UWorld* World) : Settings(GetDefault<UClientPredictionSettings>()), World(World) {
        PhysScene = World->GetPhysicsScene();
        if (PhysScene == nullptr) { return; }

        Solver = PhysScene->GetSolver();
        if (Solver == nullptr) { return; }

        // SetupPhysicsScene();
        // CreateCallbacks();
    }

    void FWorldManager::SetupPhysicsScene() {
        Solver->EnableAsyncMode(Settings->FixedDt);
        check(Solver->IsUsingAsyncResults());

        // TODO Investigate if InUseCollisionResimCache can be used
        RewindBufferSize = FMath::CeilToInt32(static_cast<float>(Settings->HistoryTimeMs) / 1000.0 / Settings->FixedDt);
        Solver->EnableRewindCapture(RewindBufferSize, false, MakeUnique<FChaosRewindCallback>());
        check(Solver->IsDetemerministic());

        UPhysicsSettings::Get()->MaxPhysicsDeltaTime = Settings->MaxPhysicsTime;

        // We require error correction duration to be zero so that when a simulation ends there will be no interpolation.
        // If this was nonzero the auto proxy would be in between where it was when it performed the rollback and the true end position, which is not desired.
        if (!GEngine->Exec(World, TEXT("p.RenderInterp.ErrorCorrectionDuration 0"))) {
            UE_LOG(LogTemp, Error, TEXT("Failed to set error correction duration!"));
        }
    }

    void FWorldManager::CreateCallbacks() {
        ChaosRewindCallback = static_cast<FChaosRewindCallback*>(Solver->GetRewindCallback());
        check(ChaosRewindCallback);

        ChaosRewindCallback->ProcessInputs_ExternalDelegate.BindRaw(this, &FWorldManager::ProcessInputs_External);
        ChaosRewindCallback->ProcessInputs_InternalDelegate.BindRaw(this, &FWorldManager::ProcessInputs_Internal);
        ChaosRewindCallback->TriggerRewindIfNeeded_InternalDelegate.BindRaw(this, &FWorldManager::TriggerRewindIfNeeded_Internal);

        PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &FWorldManager::PostAdvance_Internal));
        PostPhysSceneTickDelegate = PhysScene->OnPhysScenePostTick.AddRaw(this, &FWorldManager::OnPhysScenePostTick);
    }

    void FWorldManager::CreateReplicationManagerForPlayer(APlayerController* PlayerController) {
        if (PlayerController->GetNetConnection() == nullptr) {
            return;
        }

        // TODO investigate if swapping player controllers will cause issues here
        FActorSpawnParameters SpawnParameters{};
        SpawnParameters.Owner = PlayerController;
        SpawnParameters.Name = FName(PlayerController->GetName().Append("_ClientPredictionReplicationManager"));
        SpawnParameters.ObjectFlags |= EObjectFlags::RF_Transient;

        AClientPredictionReplicationManager* Manger = World->SpawnActor<AClientPredictionReplicationManager>(SpawnParameters);
        check(Manger)

        FScopeLock ManagerLock(&ManagersMutex);

        check(LocalReplicationManager == nullptr);
        ReplicationManagers.Add(PlayerController, Manger);

        Manger->SetStateManager(&StateManager);
    }

    void FWorldManager::DestroyReplicationManagerForPlayer(AController* Controller) {
        const APlayerController* PlayerController = reinterpret_cast<APlayerController*>(Controller);
        if (ReplicationManagers.Contains(PlayerController)) {
            ReplicationManagers[PlayerController]->Destroy();
            ReplicationManagers.Remove(PlayerController);
        }
    }

    void FWorldManager::RegisterLocalReplicationManager(AClientPredictionReplicationManager* Manager) {
        FScopeLock ManagerLock(&ManagersMutex);

        check(LocalReplicationManager == nullptr);
        check(ReplicationManagers.IsEmpty());

        LocalReplicationManager = Manager;
        LocalReplicationManager->SetStateManager(&StateManager);
    }

    void FWorldManager::AddTickCallback(ITickCallback* Callback) {
        FScopeLock Lock(&CallbacksMutex);
        TickCallbacks.Add(Callback);
    }

    void FWorldManager::RemoveTickCallback(ITickCallback* Callback) {
        FScopeLock Lock(&CallbacksMutex);
        if (TickCallbacks.Contains(Callback)) {
            TickCallbacks.Remove(Callback);
        }
    }

    void FWorldManager::AddRewindCallback(IRewindCallback* Callback) {
        FScopeLock Lock(&CallbacksMutex);
        RewindCallback = Callback;
    }

    void FWorldManager::RemoveRewindCallback(IRewindCallback* Callback) {
        FScopeLock Lock(&CallbacksMutex);
        if (RewindCallback == Callback) {
            RewindCallback = nullptr;
        }
    }

    void FWorldManager::SetTimeDilation(const Chaos::FReal TimeDilation) const {
        if (PhysScene == nullptr) { return; }
        PhysScene->SetNetworkDeltaTimeScale(TimeDilation);
    }

    FWorldManager::~FWorldManager() {
        if (PhysScene == nullptr || Solver == nullptr) { return; }

        Solver->RemovePostAdvanceCallback(PostAdvanceDelegate);
        PhysScene->OnPhysScenePostTick.Remove(PostPhysSceneTickDelegate);
    }

    // Rewind callbacks

    void FWorldManager::FChaosRewindCallback::ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
        ProcessInputs_ExternalDelegate.ExecuteIfBound(PhysicsStep);
    }

    void FWorldManager::FChaosRewindCallback::ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
        ProcessInputs_InternalDelegate.ExecuteIfBound(PhysicsStep);
    }

    int32 FWorldManager::FChaosRewindCallback::TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) {
        if (!TriggerRewindIfNeeded_InternalDelegate.IsBound()) { return INDEX_NONE; }
        return TriggerRewindIfNeeded_InternalDelegate.Execute(LatestStepCompleted);
    }

    // Tick callbacks

    void FWorldManager::ProcessInputs_External(int32 PhysicsStep) {
        const Chaos::FReal Dt = Solver->GetLastDt();

        FScopeLock Lock(&CallbacksMutex);
        TSet<ITickCallback*> CurrentTickCallbacks = TickCallbacks;

        for (ITickCallback* Callback : CurrentTickCallbacks) {
            Callback->PrepareTickGameThread(PhysicsStep, Dt);
        }
    }

    void FWorldManager::ProcessInputs_Internal(int32 PhysicsStep) {
        const Chaos::FReal Dt = Solver->GetLastDt();

        CachedLastTickNumber = PhysicsStep;
        CachedSolverStartTime = Solver->GetSolverTime();

        FScopeLock Lock(&CallbacksMutex);
        TSet<ITickCallback*> CurrentTickCallbacks = TickCallbacks;

        const Chaos::FReal TickEndTime = CachedSolverStartTime + Dt;
        for (ITickCallback* Callback : CurrentTickCallbacks) {
            Callback->PreTickPhysicsThread(PhysicsStep, Dt, CachedSolverStartTime, TickEndTime);
        }
    }

    void FWorldManager::PostAdvance_Internal(Chaos::FReal Dt) {
        FScopeLock Lock(&CallbacksMutex);
        TSet<ITickCallback*> CurrentTickCallbacks = TickCallbacks;

        for (ITickCallback* Callback : CurrentTickCallbacks) {
            Callback->PostTickPhysicsThread(CachedLastTickNumber, Dt);
        }

        StateManager.ProduceData(CachedLastTickNumber);

        {
            FScopeLock ManagerLock(&ManagersMutex);
            for (const auto& Pair : ReplicationManagers) {
                Pair.Value->PostTickAuthority(CachedLastTickNumber);
            }
        }

        StateManager.ReleasedProducedData(CachedLastTickNumber);
    }

    void FWorldManager::OnPhysScenePostTick(FChaosScene* /*TickedPhysScene*/) {
        const Chaos::FReal ResultsTime = Solver->GetPhysicsResultsTime_External();

        const Chaos::FReal Dt = LastResultsTime == -1.0 ? 0.0 : ResultsTime - LastResultsTime;
        check(Dt >= 0.0)

        {
            // Update the simulated proxy time
            FScopeLock ManagerLock(&ManagersMutex);
            if (LocalReplicationManager != nullptr) {
                LocalReplicationManager->PostSceneTickGameThreadRemote();
            }
        }

        {
            // We copy the current tick callbacks because the end of a simulation can result in a tick callback being removed.
            FScopeLock Lock(&CallbacksMutex);
            TSet<ITickCallback*> CurrentTickCallbacks = TickCallbacks;

            for (ITickCallback* Callback : CurrentTickCallbacks) {
                Callback->PostPhysicsGameThread(ResultsTime, Dt);
            }
        }

        LastResultsTime = ResultsTime;

        FScopeLock ManagerLock(&ManagersMutex);
        for (const auto& Pair : ReplicationManagers) {
            Pair.Value->PostSceneTickGameThreadAuthority();
        }
    }

    int32 FWorldManager::TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) {
        FScopeLock Lock(&CallbacksMutex);
        if (RewindCallback == nullptr) { return INDEX_NONE; }

        const Chaos::FRewindData& RewindData = *Solver->GetRewindData();
        return RewindCallback->GetRewindTickNumber(CurrentTickNumber, RewindData);
    }
}
