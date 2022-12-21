#pragma once

#include "World/ClientPredictionWorldManager.h"
#include "World/ClientPredictionTickCallback.h"

#include "PBDRigidsSolver.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API float ClientPredictionFixedDt = 1.0 / 60.0;
    FAutoConsoleVariableRef CVarClientPredictionFixedDt(TEXT("cp.FixedDt"), ClientPredictionFixedDt,
                                                        TEXT("The fixed timestep for ClientPrediction. This is also used as the async physics step"));

    CLIENTPREDICTION_API float ClientPredictionMaxPhysicsTime = 1.0;
    FAutoConsoleVariableRef CVarClientPredictionMaxPhysicsTime(TEXT("cp.MaxPhysicsTime"), ClientPredictionMaxPhysicsTime,
                                                               TEXT("The maximum time physics can be simulated per frame. "
                                                                   "This is set to a high value to make sure that clients with low framerates still simulate enough "
                                                                   "physics ticks to stay relatively in-sync with the server, and should probably remain untouched."));

    CLIENTPREDICTION_API int32 ClientPredictionHistoryTimeMs = 500;
    FAutoConsoleVariableRef CVarClientPredictionHistoryTimeMs(TEXT("cp.RewindHistoryTime"), ClientPredictionHistoryTimeMs, TEXT("The amount of time (in ms) to store for rewind"));

    CLIENTPREDICTION_API int32 ClientPredictionMaxForcedSimulationTicks = 50;
    FAutoConsoleVariableRef CVarClientPredictionMaxForcedSimulationTicks(TEXT("cp.MaxForceSimulationTicks"), ClientPredictionMaxForcedSimulationTicks, TEXT("The maximum number of forced simulation ticks"));

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

    FWorldManager::FWorldManager(const UWorld* World) {
        PhysScene = World->GetPhysicsScene();
        if (PhysScene == nullptr) { return; }

        Solver = PhysScene->GetSolver();
        if (Solver == nullptr) { return; }

        SetupPhysicsScene();
        CreateCallbacks();
    }

    void FWorldManager::SetupPhysicsScene() {
        Solver->EnableAsyncMode(ClientPredictionFixedDt);
        check(Solver->IsUsingAsyncResults());

        // TODO Investigate if InUseCollisionResimCache can be used
        RewindBufferSize = FMath::CeilToInt32(static_cast<float>(ClientPredictionHistoryTimeMs) / 1000.0 / ClientPredictionFixedDt);
        Solver->EnableRewindCapture(RewindBufferSize, false, MakeUnique<FChaosRewindCallback>());
        check(Solver->IsDetemerministic());

        UPhysicsSettings::Get()->MaxPhysicsDeltaTime = ClientPredictionMaxPhysicsTime;
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

    void FWorldManager::AddTickCallback(ITickCallback* Callback) { TickCallbacks.Add(Callback); }

    void FWorldManager::AddRewindCallback(IRewindCallback* Callback) {
        RewindCallback = Callback;
    }

    void FWorldManager::RemoveCallback(const void* Callback) {
        const auto* TickCallback = static_cast<const ITickCallback*>(Callback);
        if (TickCallbacks.Contains(TickCallback)) {
            TickCallbacks.Remove(TickCallback);
        }

        if (RewindCallback == static_cast<const IRewindCallback*>(Callback)) {
            RewindCallback = nullptr;
        }
    }

    void FWorldManager::SetTimeDilation(const Chaos::FReal TimeDilation) const {
        if (PhysScene == nullptr) { return; }
        PhysScene->SetNetworkDeltaTimeScale(TimeDilation);
    }

    void FWorldManager::ForceSimulate(const uint32 NumTicks) const {
        if (PhysScene == nullptr || Solver == nullptr) { return; }

        const Chaos::EThreadingModeTemp CachedThreadingMode = Solver->GetThreadingMode();
        const float CachedTimeDilation = PhysScene->GetNetworkDeltaTimeScale();

        Solver->SetThreadingMode_External(Chaos::EThreadingModeTemp::SingleThread);
        PhysScene->SetNetworkDeltaTimeScale(1.0);

        const int32 AdjustedNumTicks = FMath::Min(static_cast<int32>(NumTicks), ClientPredictionMaxForcedSimulationTicks);
        const Chaos::FReal SimulationTime = static_cast<Chaos::FReal>(AdjustedNumTicks) * Solver->GetAsyncDeltaTime();

        Solver->AdvanceAndDispatch_External(SimulationTime);
        Solver->UpdateGameThreadStructures();

        PhysScene->SetNetworkDeltaTimeScale(CachedTimeDilation);
        Solver->SetThreadingMode_External(CachedThreadingMode);
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

        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PrepareTickGameThread(PhysicsStep, Dt);
        }
    }

    void FWorldManager::ProcessInputs_Internal(int32 PhysicsStep) {
        const Chaos::FReal Dt = Solver->GetLastDt();

        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PreTickPhysicsThread(PhysicsStep, Dt);
        }

        CachedLastTickNumber = PhysicsStep;
        CachedSolverStartTime = Solver->GetSolverTime();
    }

    void FWorldManager::PostAdvance_Internal(Chaos::FReal Dt) {
        const Chaos::FReal TickEndTime = CachedSolverStartTime + Dt;

        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PostTickPhysicsThread(CachedLastTickNumber, Dt, CachedSolverStartTime, TickEndTime);
        }
    }

    void FWorldManager::OnPhysScenePostTick(FChaosScene* /*TickedPhysScene*/) {
        const Chaos::FReal ResultsTime = Solver->GetPhysicsResultsTime_External();

        const Chaos::FReal Dt = LastResultsTime == -1.0 ? 0.0 : ResultsTime - LastResultsTime;
        check(Dt >= 0.0)

        for (ITickCallback* Callback : TickCallbacks) {
            Callback->PostPhysicsGameThread(ResultsTime, Dt);
        }

        LastResultsTime = ResultsTime;
    }

    int32 FWorldManager::TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) const {
        if (RewindCallback == nullptr) { return INDEX_NONE; }

        const Chaos::FRewindData& RewindData = *Solver->GetRewindData();
        return RewindCallback->GetRewindTickNumber(CurrentTickNumber, RewindData);
    }
}
