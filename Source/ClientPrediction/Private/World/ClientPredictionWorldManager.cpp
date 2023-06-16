#pragma once

#include "World/ClientPredictionWorldManager.h"
#include "World/ClientPredictionTickCallback.h"

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

    FWorldManager::FWorldManager(const UWorld* World) : Settings(GetDefault<UClientPredictionSettings>()) {
        PhysScene = World->GetPhysicsScene();
        if (PhysScene == nullptr) { return; }

        Solver = PhysScene->GetSolver();
        if (Solver == nullptr) { return; }

        SetupPhysicsScene();
        CreateCallbacks();
    }

    void FWorldManager::SetupPhysicsScene() {
        Solver->EnableAsyncMode(Settings->FixedDt);
        check(Solver->IsUsingAsyncResults());

        // TODO Investigate if InUseCollisionResimCache can be used
        RewindBufferSize = FMath::CeilToInt32(static_cast<float>(Settings->HistoryTimeMs) / 1000.0 / Settings->FixedDt);
        Solver->EnableRewindCapture(RewindBufferSize, false, MakeUnique<FChaosRewindCallback>());
        check(Solver->IsDetemerministic());

        UPhysicsSettings::Get()->MaxPhysicsDeltaTime = Settings->MaxPhysicsTime;
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

    void FWorldManager::ForceSimulate(const uint32 NumTicks) {
        if (bIsForceSimulating) { return; }

        FScopeLock Lock(&ForcedSimulationTicksMutex);
        ForcedSimulationTicks += NumTicks;
    }

    void FWorldManager::DoForceSimulateIfNeeded() {
        if (PhysScene == nullptr || Solver == nullptr || bIsForceSimulating) { return; }

        int32 AdjustedNumTicks;
        {
            FScopeLock Lock(&ForcedSimulationTicksMutex);
            if (ForcedSimulationTicks == 0) { return; }

            AdjustedNumTicks = ForcedSimulationTicks;
            ForcedSimulationTicks = 0;
        }

        //AdjustedNumTicks =  FMath::Min(static_cast<int32>(AdjustedNumTicks), ClientPredictionMaxForcedSimulationTicks);
        const Chaos::FReal SimulationTime = static_cast<Chaos::FReal>(AdjustedNumTicks) * Solver->GetAsyncDeltaTime();
        bIsForceSimulating = true;

        const Chaos::EThreadingModeTemp CachedThreadingMode = Solver->GetThreadingMode();
        const float CachedTimeDilation = PhysScene->GetNetworkDeltaTimeScale();

        Solver->SetThreadingMode_External(Chaos::EThreadingModeTemp::SingleThread);
        PhysScene->SetNetworkDeltaTimeScale(1.0);

        Solver->AdvanceAndDispatch_External(SimulationTime);
        Solver->UpdateGameThreadStructures();

        PhysScene->SetNetworkDeltaTimeScale(CachedTimeDilation);
        Solver->SetThreadingMode_External(CachedThreadingMode);

        bIsForceSimulating = false;
    }

    FWorldManager::~FWorldManager() {
        if (PhysScene == nullptr || Solver == nullptr) { return; }

        Solver->SetRewindCallback({});
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
        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PrepareTickGameThread(PhysicsStep, Dt);
        }
    }

    void FWorldManager::ProcessInputs_Internal(int32 PhysicsStep) {
        const Chaos::FReal Dt = Solver->GetLastDt();

        FScopeLock Lock(&CallbacksMutex);
        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PreTickPhysicsThread(PhysicsStep, Dt);
        }

        CachedLastTickNumber = PhysicsStep;
        CachedSolverStartTime = Solver->GetSolverTime();
    }

    void FWorldManager::PostAdvance_Internal(Chaos::FReal Dt) {
        const Chaos::FReal TickEndTime = CachedSolverStartTime + Dt;

        FScopeLock Lock(&CallbacksMutex);
        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PostTickPhysicsThread(CachedLastTickNumber, Dt, CachedSolverStartTime, TickEndTime);
        }
    }

    void FWorldManager::OnPhysScenePostTick(FChaosScene* /*TickedPhysScene*/) {
        const Chaos::FReal ResultsTime = Solver->GetPhysicsResultsTime_External();

        const Chaos::FReal Dt = LastResultsTime == -1.0 ? 0.0 : ResultsTime - LastResultsTime;
        check(Dt >= 0.0)

        {
            FScopeLock Lock(&CallbacksMutex);
            for (ITickCallback* Callback : TickCallbacks) {
                Callback->PostPhysicsGameThread(ResultsTime, Dt);
            }
        }

        if (!bIsForceSimulating) { DoForceSimulateIfNeeded(); }
        LastResultsTime = ResultsTime;
    }

    int32 FWorldManager::TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) {
        if (bIsForceSimulating) { return INDEX_NONE; }

        FScopeLock Lock(&CallbacksMutex);
        if (RewindCallback == nullptr) { return INDEX_NONE; }

        const Chaos::FRewindData& RewindData = *Solver->GetRewindData();
        return RewindCallback->GetRewindTickNumber(CurrentTickNumber, RewindData);
    }
}
