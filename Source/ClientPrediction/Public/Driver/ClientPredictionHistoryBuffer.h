#pragma once

#include "ClientPredictionModelTypes.h"
#include "Math/UnitConversion.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FHistoryBuffer {
        explicit FHistoryBuffer(const int32 Capacity);

        void Update(const FStateWrapper<StateType>& State);
        bool GetStateAtTick(const int32 TickNumber, FStateWrapper<StateType>& OutState);
        void GetStateAtTime(const Chaos::FReal Time, StateType& OutState);

        int32 GetLatestTickNumber() {
            FScopeLock Lock(&Mutex);
            return History.IsEmpty() ? INDEX_NONE : History.Last().TickNumber;
        }

    private:
        int32 Capacity = 0;
        TArray<FStateWrapper<StateType>> History;
        FCriticalSection Mutex;
    };

    template <typename StateType>
    FHistoryBuffer<StateType>::FHistoryBuffer(const int32 Capacity) : Capacity(Capacity) {
        History.Reserve(Capacity + 1);
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::Update(const FStateWrapper<StateType>& State) {
        FScopeLock Lock(&Mutex);

        bool bUpdatedExistingState = false;
        for (FStateWrapper<StateType>& ExistingState : History) {
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
    bool FHistoryBuffer<StateType>::GetStateAtTick(const int32 TickNumber, FStateWrapper<StateType>& OutState) {
        FScopeLock Lock(&Mutex);

        for (const FStateWrapper<StateType>& State : History) {
            if (State.TickNumber == TickNumber) {
                OutState = State;
                return true;
            }
        }

        return false;
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::GetStateAtTime(const Chaos::FReal Time, StateType& OutState) {
        FScopeLock Lock(&Mutex);
        OutState = {};

        if (History.IsEmpty()) { return; }
        for (int i = 0; i < History.Num(); i++) {
            if (History[i].EndTime >= Time) {
                if (i != 0) {
                    const FStateWrapper<StateType>& Start = History[i - 1];
                    const FStateWrapper<StateType>& End = History[i];

                    const Chaos::FReal PrevEndTime = Start.EndTime;
                    const Chaos::FReal TimeFromPrevEnd = Time - PrevEndTime;
                    const Chaos::FReal TotalTime = End.EndTime - Start.EndTime;
                    const Chaos::FReal Alpha = FMath::Clamp(TimeFromPrevEnd / TotalTime, 0.0, 1.0);

                    OutState = Start.Body;
                    OutState.Interpolate(End.Body, Alpha);
                    return;
                }

                OutState = History[0].Body;
                return;
            }
        }

        OutState = History.Last().Body;
    }
}
