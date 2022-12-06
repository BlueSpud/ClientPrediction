#pragma once

#include "World/ClientPredictionWorldManager.h"

#include "ClientPredictionTickCallback.h"
#include "PBDRigidsSolver.h"

CLIENTPREDICTION_API float ClientPredictionFixedDt = 1.0 / 60.0;
FAutoConsoleVariableRef CVarClientPredictionFixedDt(TEXT("cp.FixedDt"), ClientPredictionFixedDt, TEXT("The fixed timestep for ClientPrediction. This is also used as the async physics step"));

CLIENTPREDICTION_API uint32 ClientPredictionHistoryTimeMs = 400;
FAutoConsoleVariableRef CVarClientPredictionHistoryTimeMs(TEXT("cp.RewindHistoryTime"), ClientPredictionFixedDt, TEXT("The amount of time to store for rewind"));

TMap<UWorld*, FClientPredictionWorldManager*> FClientPredictionWorldManager::Managers;

// Initialization

FClientPredictionWorldManager* FClientPredictionWorldManager::InitializeWorld(UWorld* World) {
	check(!Managers.Contains(World));

	FClientPredictionWorldManager* Manager = new FClientPredictionWorldManager(World);
	Managers.Add(World, Manager);

	return Manager;
}

void FClientPredictionWorldManager::CleanupWorld(const UWorld* World) {
	if (!Managers.Contains(World)) { return; }

	const FClientPredictionWorldManager* WorldManager = Managers[World];
	Managers.Remove(World);

	delete WorldManager;
}

FClientPredictionWorldManager::FClientPredictionWorldManager(const UWorld* World) {
	const FPhysScene* PhysScene = World->GetPhysicsScene();
	if (PhysScene == nullptr) { return; }

	Solver = PhysScene->GetSolver();
	if (Solver == nullptr) { return; }

	SetupPhysicsScene();
	CreateCallbacks();
}

void FClientPredictionWorldManager::SetupPhysicsScene() const {
	Solver->EnableAsyncMode(ClientPredictionFixedDt);
	check(Solver->IsUsingAsyncResults());

	// TODO Investigate if InUseCollisionResimCache can be used
	const int32 NumRewindFrames = FMath::CeilToInt32(static_cast<float>(ClientPredictionHistoryTimeMs) / 1000.0 / ClientPredictionFixedDt);
	Solver->EnableRewindCapture(NumRewindFrames, false, MakeUnique<FRewindCallback>());
	check(Solver->IsDetemerministic());
}

void FClientPredictionWorldManager::CreateCallbacks() {
	RewindCallback = static_cast<FRewindCallback*>(Solver->GetRewindCallback());
	check(RewindCallback);

	RewindCallback->ProcessInputs_ExternalDelegate.BindRaw(this, &FClientPredictionWorldManager::ProcessInputs_External);
	RewindCallback->ProcessInputs_InternalDelegate.BindRaw(this, &FClientPredictionWorldManager::ProcessInputs_Internal);
	RewindCallback->TriggerRewindIfNeeded_InternalDelegate.BindRaw(this, &FClientPredictionWorldManager::TriggerRewindIfNeeded_Internal);

	PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &FClientPredictionWorldManager::PostAdvance_Internal));
}

FClientPredictionWorldManager::~FClientPredictionWorldManager() {
	if (Solver == nullptr) { return; }

	Solver->SetRewindCallback({});
	Solver->RemovePostAdvanceCallback(PostAdvanceDelegate);
}

// Rewind callbacks

void FClientPredictionWorldManager::FRewindCallback::ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
	ProcessInputs_ExternalDelegate.ExecuteIfBound(PhysicsStep);
}

void FClientPredictionWorldManager::FRewindCallback::ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
	ProcessInputs_InternalDelegate.ExecuteIfBound(PhysicsStep);
}

int32 FClientPredictionWorldManager::FRewindCallback::TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) {
	if (!TriggerRewindIfNeeded_InternalDelegate.IsBound()) { return INDEX_NONE; }
	return TriggerRewindIfNeeded_InternalDelegate.Execute(LatestStepCompleted);
}

// Tick callbacks

void FClientPredictionWorldManager::ProcessInputs_External(int32 PhysicsStep) {
	const Chaos::FReal Dt = Solver->GetLastDt();

	for (IClientPredictionTickCallback* Callback : TickCallbacks) {
		Callback->PreTickGameThread(PhysicsStep, Dt);
	}
}

void FClientPredictionWorldManager::ProcessInputs_Internal(int32 PhysicsStep) {
	const Chaos::FReal Dt = Solver->GetLastDt();

	for (IClientPredictionTickCallback* Callback : TickCallbacks) {
		Callback->PreTickPhysicsThread(PhysicsStep, Dt);
	}
}

void FClientPredictionWorldManager::PostAdvance_Internal(Chaos::FReal Dt) {
	for (IClientPredictionTickCallback* Callback : TickCallbacks) {
		Callback->PostTickPhysicsThread(CachedLastTickNumber, Dt);
	}
}

int32 FClientPredictionWorldManager::TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) {
	const Chaos::FReal Dt = Solver->GetLastDt();

	int32 RewindTickNumber = INDEX_NONE;
	for (IClientPredictionTickCallback* Callback : TickCallbacks) {
		RewindTickNumber = FMath::Min(RewindTickNumber, Callback->GetRewindTickNumber(CurrentTickNumber, Dt));
	}

	return RewindTickNumber;
}
