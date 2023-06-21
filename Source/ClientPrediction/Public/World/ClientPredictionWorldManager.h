#pragma once

#include "CoreMinimal.h"
#include "RewindData.h"

#include "ClientPredictionSettings.h"
#include "Data/ClientPredictionStateManager.h"

class AClientPredictionReplicationManager;

namespace ClientPrediction {
	struct CLIENTPREDICTION_API FWorldManager {
	private:
		static TMap<class UWorld*, FWorldManager*> Managers;

	public:
		static FWorldManager* InitializeWorld(class UWorld* World);
		static FWorldManager* ManagerForWorld(const class UWorld* World);
		static void CleanupWorld(const class UWorld* World);

		virtual ~FWorldManager();

	private:
		explicit FWorldManager(class UWorld* World);
		void SetupPhysicsScene();
		void CreateCallbacks();

	public:
		void CreateReplicationManagerForPlayer(class APlayerController* PlayerController);
		void DestroyReplicationManagerForPlayer(class AController* Controller);
		void RegisterLocalReplicationManager(class APlayerController* PlayerController, AClientPredictionReplicationManager* Manager);

		void AddTickCallback(class ITickCallback* Callback);
		void RemoveTickCallback(class ITickCallback* Callback);

		void AddRewindCallback(class IRewindCallback* Callback);
		void RemoveRewindCallback(class IRewindCallback* Callback);

		int32 GetRewindBufferSize() const { return RewindBufferSize; }

		// These should only be called from the game thread
		void SetTimeDilation(const Chaos::FReal TimeDilation) const;

		void ForceSimulate(const uint32 NumTicks);

		FStateManager& GetStateManager() { return StateManager; }

	private:
		void DoForceSimulateIfNeeded();

		DECLARE_DELEGATE_OneParam(FTickCallback, int32);
		DECLARE_DELEGATE_RetVal_OneParam(int32, FRewindTickCallback, int32);

		struct FChaosRewindCallback : public Chaos::IRewindCallback {
			/** [Game thread] Called before each tick */
			virtual void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override;

			/** [Physics thread] Called before each tick */
			virtual void ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override;

			/** [Physics thread] Called to determine if a rewind is needed, INDEX_NONE is no rewind. */
			virtual int32 TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) override;

			FTickCallback ProcessInputs_ExternalDelegate;
			FTickCallback ProcessInputs_InternalDelegate;
			FRewindTickCallback TriggerRewindIfNeeded_InternalDelegate;
		};

		/** [Physics thread] Called before each tick */
		virtual void ProcessInputs_Internal(int32 PhysicsStep);

		/** [Game thread] Called before each tick */
		virtual void ProcessInputs_External(int32 PhysicsStep);

		/** [Physics thread] Called after each tick */
		void PostAdvance_Internal(Chaos::FReal Dt);

		/** [Physics thread] Called after physics finishes */
		void OnPhysScenePostTick(FChaosScene* TickedPhysScene);

		/** [Physics thread] Called to determine if a rewind is needed, INDEX_NONE is no rewind. */
		int32 TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber);

		const UClientPredictionSettings* Settings = nullptr;

		class UWorld* World = nullptr;
		TMap<class APlayerController*, AClientPredictionReplicationManager*> ReplicationManagers;
		AClientPredictionReplicationManager* LocalReplicationManager = nullptr;

		FPhysScene* PhysScene = nullptr;
		Chaos::FPhysicsSolver* Solver = nullptr;
		FChaosRewindCallback* ChaosRewindCallback = nullptr;
		int32 RewindBufferSize = 0;

		FStateManager StateManager{};

		FDelegateHandle PostAdvanceDelegate;
		FDelegateHandle PostPhysSceneTickDelegate;

		int32 CachedLastTickNumber = INDEX_NONE;
		Chaos::FReal CachedSolverStartTime = 0.0;
		Chaos::FReal LastResultsTime = -1.0;

		FCriticalSection CallbacksMutex;
		TSet<class ITickCallback*> TickCallbacks;
		class IRewindCallback* RewindCallback = nullptr;

		FCriticalSection ForcedSimulationTicksMutex;
		uint32 ForcedSimulationTicks = 0;
		TAtomic<bool> bIsForceSimulating = false;
	};
}
