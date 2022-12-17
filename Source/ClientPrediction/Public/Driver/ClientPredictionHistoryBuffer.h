#pragma once

#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FHistoryBuffer {
        explicit FHistoryBuffer(const int32 Capacity);

        void Update(const FPhysicsState<StateType>& State);
        void GetStateAtTick(const int32 TickNumber, FPhysicsState<StateType>& OutState);
        void GetStateAtTime(const Chaos::FReal Time, StateType& OutState);

    private:
        int32 Capacity = 0;
        TArray<FPhysicsState<StateType>> History;
        FCriticalSection Mutex;
    };

    template <typename StateType>
    FHistoryBuffer<StateType>::FHistoryBuffer(const int32 Capacity) : Capacity(Capacity) {
        History.Reserve(Capacity + 1);
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::Update(const FPhysicsState<StateType>& State) {
        FScopeLock Lock(&Mutex);

        bool bUpdatedExistingState = false;
        for (FPhysicsState<StateType>& ExistingState : History) {
            if (ExistingState.TickNumber == State.TickNumber) {
                ExistingState = State;
                bUpdatedExistingState = true;
            }
        }

        if (!bUpdatedExistingState) { History.Add(State); }
        while (History.Num() > Capacity) {
            History.RemoveAt(0);
        }
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::GetStateAtTick(const int32 TickNumber, FPhysicsState<StateType>& OutState) {
        FScopeLock Lock(&Mutex);

        for (const FPhysicsState<StateType>& State : History) {
            if (State.TickNumber == TickNumber) {
                OutState = State;
                return;
            }
        }

        check(false)
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::GetStateAtTime(const Chaos::FReal Time, StateType& OutState) {
        FScopeLock Lock(&Mutex);
        OutState = {};

        if (History.IsEmpty()) { return; }
        for (int i = 0; i < History.Num(); i++) {
            if (History[i].EndTime >= Time) {
                if (i != 0) {
                    const FPhysicsState<StateType>& Start = History[i - 1];
                    const FPhysicsState<StateType>& End = History[i];

                    const Chaos::FReal PrevEndTime = Start.EndTime;
                    const Chaos::FReal TimeFromPrevEnd = Time - PrevEndTime;
                    const Chaos::FReal TotalTime = End.EndTime - Start.EndTime;
                    const Chaos::FReal Alpha = FMath::Clamp(TimeFromPrevEnd / TotalTime, 0.0, 1.0);

                    OutState = Start.Body;
                    OutState.Interpolate(End.Body, Alpha);
                }
                else { OutState = History[0].Body; }
            }
        }

        OutState = History.Last().Body;
    }
}
