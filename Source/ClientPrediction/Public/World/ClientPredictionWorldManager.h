#pragma once

#include "CoreMinimal.h"
#include "RewindData.h"

extern CLIENTPREDICTION_API float ClientPredictionFixedDt;
extern CLIENTPREDICTION_API uint32 ClientPredictionHistoryTimeMs;

struct CLIENTPREDICTION_API FClientPredictionWorldManager {

public:

	static FClientPredictionWorldManager* InitializeWorld(class UWorld* World);
	static void CleanupWorld(const class UWorld* World);

	virtual ~FClientPredictionWorldManager();

private:
	explicit FClientPredictionWorldManager(const class UWorld* World);
	void SetupPhysicsScene() const;
	void CreateCallbacks();

	static TMap<class UWorld*, FClientPredictionWorldManager*> Managers;

	DECLARE_DELEGATE_OneParam(FTickCallback, int32);
	DECLARE_DELEGATE_RetVal_OneParam(int32, FRewindTickCallback, int32);

	struct FRewindCallback : public Chaos::IRewindCallback {

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

	/** [Physics thread] Called to determine if a rewind is needed, INDEX_NONE is no rewind. */
	int32 TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber);

	Chaos::FPhysicsSolver* Solver = nullptr;
	FRewindCallback* RewindCallback = nullptr;
	FDelegateHandle PostAdvanceDelegate;

	int32 CachedLastTickNumber = INDEX_NONE;
	TArray<class IClientPredictionTickCallback*> TickCallbacks;
};
