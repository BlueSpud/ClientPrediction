﻿#pragma once

#include "CoreMinimal.h"
#include "RewindData.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API float ClientPredictionFixedDt;
    extern CLIENTPREDICTION_API float ClientPredictionMaxPhysicsTime;
    extern CLIENTPREDICTION_API int32 ClientPredictionHistoryTimeMs;
    extern CLIENTPREDICTION_API int32 ClientPredictionMaxedForceSimulationTicks;

    struct CLIENTPREDICTION_API FWorldManager {
    private:
        static TMap<class UWorld*, FWorldManager*> Managers;

    public:
        static FWorldManager* InitializeWorld(class UWorld* World);
        static FWorldManager* ManagerForWorld(const class UWorld* World);
        static void CleanupWorld(const class UWorld* World);

        virtual ~FWorldManager();

    private:
        explicit FWorldManager(const class UWorld* World);
        void SetupPhysicsScene();
        void CreateCallbacks();

    public:
        void AddTickCallback(class ITickCallback* Callback);
        void AddRewindCallback(class IRewindCallback* Callback);

        void RemoveTickCallback(const class ITickCallback* Callback);
        void RemoveRewindCallback(const class IRewindCallback* Callback);

        int32 GetRewindBufferSize() const { return RewindBufferSize; }

        // These should only be called from the game thread
        void SetTimeDilation(const Chaos::FReal TimeDilation) const;
        void ForceSimulate(const uint32 NumTicks) const;

    private:
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
        int32 TriggerRewindIfNeeded_Internal(int32 CurrentTickNumber) const;

        FPhysScene* PhysScene = nullptr;
        Chaos::FPhysicsSolver* Solver = nullptr;
        FChaosRewindCallback* ChaosRewindCallback = nullptr;
        int32 RewindBufferSize = 0;

        FDelegateHandle PostAdvanceDelegate;
        FDelegateHandle PostPhysSceneTickDelegate;

        int32 CachedLastTickNumber = INDEX_NONE;
        Chaos::FReal CachedSolverStartTime = 0.0;
        Chaos::FReal LastResultsTime = -1.0;

        TSet<class ITickCallback*> TickCallbacks;
        class IRewindCallback* RewindCallback = nullptr;
    };
}