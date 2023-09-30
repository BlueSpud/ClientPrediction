#pragma once

#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FHistoryEntry {
        FStateWrapper<StateType> State;

        /** This is set to true the first time that the state is used as the start for interpolation. */
        bool bHasBeenConsumed = false;

        /** This is the time spent in the buffer before the state was consumed. */
        Chaos::FReal TimeWaitingToBeConsumed = 0.0;
    };

    template <typename StateType>
    struct FHistoryBuffer {
        explicit FHistoryBuffer(const int32 Capacity);

        void Update(const FStateWrapper<StateType>& State);
        bool GetStateAtTick(const int32 TickNumber, FStateWrapper<StateType>& OutState);
        void GetStateAtTime(const Chaos::FReal Time, StateType& OutState, bool& bIsFinalState);
        void RemoveAfterTick(const int32 TickNumber);

        Chaos::FReal GetAverageTimeToConsumeState();
        void UpdateTimeWaitingToBeConsumed(const Chaos::FReal Dt);

        int32 GetLatestTickNumber() {
            FScopeLock Lock(&Mutex);
            return History.IsEmpty() ? INDEX_NONE : History.Last().State.TickNumber;
        }

    private:
        int32 Capacity = 0;
        TArray<FHistoryEntry<StateType>> History;
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
        for (FHistoryEntry<StateType>& Entry : History) {
            FStateWrapper<StateType>& ExistingState = Entry.State;

            if (ExistingState.TickNumber == State.TickNumber) {
                ExistingState = State;
                bUpdatedExistingState = true;
            }
        }

        if (!bUpdatedExistingState) {
            // Ensure that history is contiguous
            check(History.IsEmpty() || History.Last().State.TickNumber == 0 || History.Last().State.TickNumber + 1 == State.TickNumber)

            History.Add({State, false, 0.0});
        }

        while (History.Num() > Capacity) {
            History.RemoveAt(0);
        }
    }

    template <typename StateType>
    bool FHistoryBuffer<StateType>::GetStateAtTick(const int32 TickNumber, FStateWrapper<StateType>& OutState) {
        FScopeLock Lock(&Mutex);

        for (FHistoryEntry<StateType>& Entry : History) {
            const FStateWrapper<StateType>& State = Entry.State;

            if (State.TickNumber == TickNumber) {
                OutState = State;
                return true;
            }
        }

        return false;
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::GetStateAtTime(const Chaos::FReal Time, StateType& OutState, bool& bIsFinalState) {
        FScopeLock Lock(&Mutex);

        OutState = {};
        bIsFinalState = false;

        if (History.IsEmpty()) { return; }
        for (int i = 0; i < History.Num(); i++) {
            if (History[i].State.EndTime >= Time) {
                if (i != 0) {
                    const FStateWrapper<StateType>& Start = History[i - 1].State;
                    const FStateWrapper<StateType>& End = History[i].State;

                    History[i - 1].bHasBeenConsumed = true;
                    OutState = Start.Body;

                    const Chaos::FReal Denominator = End.EndTime - End.StartTime;
                    const Chaos::FReal Alpha = Denominator != 0.0 ? FMath::Min(1.0, (Time - End.StartTime) / Denominator) : 1.0;
                    OutState.Interpolate(End.Body, Alpha);

                    return;
                }

                OutState = History[0].State.Body;
                History[0].bHasBeenConsumed = true;

                return;
            }
        }

        OutState = History.Last().State.Body;
        bIsFinalState = History.Last().State.bIsFinalState;
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::RemoveAfterTick(const int32 TickNumber) {
        FScopeLock Lock(&Mutex);

        for (int i = 0; i < History.Num();) {
            if (History[i].State.TickNumber > TickNumber) {
                History.RemoveAt(i);
                continue;
            }

            ++i;
        }
    }

    template <typename StateType>
    Chaos::FReal FHistoryBuffer<StateType>::GetAverageTimeToConsumeState() {
        Chaos::FReal TotalTime = 0.0;
        int32 TotalSamples = 0;

        for (; TotalSamples < History.Num(); ++TotalSamples) {
            if (!History[TotalSamples].bHasBeenConsumed) {
                break;
            }

            TotalTime += History[TotalSamples].TimeWaitingToBeConsumed;
        }

        return TotalTime / static_cast<Chaos::FReal>(TotalSamples);
    }

    template <typename StateType>
    void FHistoryBuffer<StateType>::UpdateTimeWaitingToBeConsumed(const Chaos::FReal Dt) {
        FScopeLock Lock(&Mutex);

        for (int i = History.Num() - 1; i >= 0; --i) {
            if (History[i].bHasBeenConsumed) {
                break;
            }

            History[i].TimeWaitingToBeConsumed += Dt;
        }
    }
}
