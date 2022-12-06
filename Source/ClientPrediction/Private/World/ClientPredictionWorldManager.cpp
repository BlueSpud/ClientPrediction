﻿#pragma once

#include "World/ClientPredictionWorldManager.h"
#include "World/ClientPredictionTickCallback.h"

#include "PBDRigidsSolver.h"

namespace ClientPrediction {

	CLIENTPREDICTION_API float ClientPredictionFixedDt = 1.0 / 60.0;
	FAutoConsoleVariableRef CVarClientPredictionFixedDt(TEXT("cp.FixedDt"), ClientPredictionFixedDt, TEXT("The fixed timestep for ClientPrediction. This is also used as the async physics step"));

	CLIENTPREDICTION_API uint32 ClientPredictionHistoryTimeMs = 400;
	FAutoConsoleVariableRef CVarClientPredictionHistoryTimeMs(TEXT("cp.RewindHistoryTime"), ClientPredictionFixedDt, TEXT("The amount of time to store for rewind"));

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
		const FPhysScene* PhysScene = World->GetPhysicsScene();
		if (PhysScene == nullptr) { return; }

		Solver = PhysScene->GetSolver();
		if (Solver == nullptr) { return; }

		SetupPhysicsScene();
		CreateCallbacks();
	}

	void FWorldManager::SetupPhysicsScene() const {
		Solver->EnableAsyncMode(ClientPredictionFixedDt);
		check(Solver->IsUsingAsyncResults());

		// TODO Investigate if InUseCollisionResimCache can be used
		const int32 NumRewindFrames = FMath::CeilToInt32(static_cast<float>(ClientPredictionHistoryTimeMs) / 1000.0 / ClientPredictionFixedDt);
		Solver->EnableRewindCapture(NumRewindFrames, false, MakeUnique<FRewindCallback>());
		check(Solver->IsDetemerministic());
	}

	void FWorldManager::CreateCallbacks() {
		RewindCallback = static_cast<FRewindCallback*>(Solver->GetRewindCallback());
		check(RewindCallback);

		RewindCallback->ProcessInputs_ExternalDelegate.BindRaw(this, &FWorldManager::ProcessInputs_External);
		RewindCallback->ProcessInputs_InternalDelegate.BindRaw(this, &FWorldManager::ProcessInputs_Internal);
		RewindCallback->TriggerRewindIfNeeded_InternalDelegate.BindRaw(this, &FWorldManager::TriggerRewindIfNeeded_Internal);

		PostAdvanceDelegate = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateRaw(this, &FWorldManager::PostAdvance_Internal));
	}

	void FWorldManager::AddTickCallback(ITickCallback* Callback) { TickCallbacks.Add(Callback); }
	void FWorldManager::RemoveTickCallback(const ITickCallback* Callback) { TickCallbacks.Remove(Callback); }

	FWorldManager::~FWorldManager() {
		if (Solver == nullptr) { return; }

		Solver->SetRewindCallback({});
		Solver->RemovePostAdvanceCallback(PostAdvanceDelegate);
	}

	// Rewind callbacks

	void FWorldManager::FRewindCallback::ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
		ProcessInputs_ExternalDelegate.ExecuteIfBound(PhysicsStep);
	}

	void FWorldManager::FRewindCallback::ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) {
		ProcessInputs_InternalDelegate.ExecuteIfBound(PhysicsStep);
	}

	int32 FWorldManager::FRewindCallback::TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) {
		if (!TriggerRewindIfNeeded_InternalDelegate.IsBound()) { return INDEX_NONE; }
		return TriggerRewindIfNeeded_InternalDelegate.Execute(LatestStepCompleted);
	}

	// Tick callbacks

	void FWorldManager::ProcessInputs_External(int32 PhysicsStep) {
		const Chaos::FReal Dt = Solver->GetLastDt();

		for (ITickCallback* Callback : TickCallbacks) {
			Callback->PreTickGameThread(PhysicsStep, Dt);
		}
	}

	void FWorldManager::ProcessInputs_Internal(int32 PhysicsStep) {
		const Chaos::FReal Dt = Solver->GetLastDt();

		for (ITickCallback* Callback : TickCallbacks) {
			Callback->PreTickPhysicsThread(PhysicsStep, Dt);
		}
	}

	void FWorldManager::PostAdvance_Internal(Chaos::FReal Dt) {
		for (ITickCallback* Callback : TickCallbacks) {
			Callback->PostTickPhysicsThread(CachedLastTickNumber, Dt);
		}
	}

	int32 FWorldManager::TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) {
		const Chaos::FReal Dt = Solver->GetLastDt();

		int32 RewindTickNumber = INDEX_NONE;
		for (ITickCallback* Callback : TickCallbacks) {
			RewindTickNumber = FMath::Min(RewindTickNumber, Callback->GetRewindTickNumber(CurrentTickNumber, Dt));
		}

		return RewindTickNumber;
	}
}
