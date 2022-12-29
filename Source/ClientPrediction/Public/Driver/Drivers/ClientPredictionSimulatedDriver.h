#pragma once

#include "Driver/ClientPredictionHistoryBuffer.h"
#include "Driver/ClientPredictionModelDriver.h"

namespace ClientPrediction {
    template <typename StateType>
    struct FEvent {
        FStateWrapper<StateType> State{};
        uint8 Events = 0;
    };

    template <typename InputType, typename StateType>
    class FSimulatedModelDriver : public IModelDriver<InputType, StateType> {
    public:
        FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, int32 HistoryBufferSize);

    protected:
        class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

    public:
        void PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt);
        void PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime);

    protected:
        void InterpolateStateGameThread(Chaos::FReal SimTime, Chaos::FReal Dt);
        void UpdateHistory(const FStateWrapper<StateType>& State);

    protected:
        UPrimitiveComponent* UpdatedComponent = nullptr;
        IModelDriverDelegate<InputType, StateType>* Delegate = nullptr;
        FHistoryBuffer<StateType> History;

        FInputPacketWrapper<InputType> CurrentInput{}; // Only used on physics thread
        FStateWrapper<StateType> CurrentState{}; // Only used on physics thread
        FStateWrapper<StateType> LastState{}; // Only used on physics thread

        // Events have their own queue since we do not want to dispatch events more than once during re-simulates if they are the same.
        FCriticalSection EventQueueMutex;
        TArray<FEvent<StateType>> EventQueue; // Written from the physics thread and consumed on the game thread
    };

    template <typename InputType, typename StateType>
    FSimulatedModelDriver<InputType, StateType>::FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                                                                       int32 HistoryBufferSize)
        : UpdatedComponent(UpdatedComponent), Delegate(Delegate), History(HistoryBufferSize) {
        check(UpdatedComponent);
        check(Delegate);

        Delegate->GenerateInitialState(CurrentState);
        CurrentState.TickNumber = INDEX_NONE;
        CurrentState.InputPacketTickNumber = INDEX_NONE;
        CurrentState.Events = 0;

        LastState = CurrentState;
        UpdateHistory(CurrentState);
    }

    template <typename InputType, typename StateType>
    Chaos::FRigidBodyHandle_Internal* FSimulatedModelDriver<InputType, StateType>::GetPhysicsHandle() const {
        FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
        if (BodyInstance == nullptr) { return nullptr; }

        Chaos::FRigidBodyHandle_Internal* PhysicsHandle = BodyInstance->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
        check(PhysicsHandle);

        return PhysicsHandle;
    }

    template <typename InputType, typename StateType>
    void FSimulatedModelDriver<InputType, StateType>::PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt) {
        LastState = CurrentState;

        CurrentState = {};
        CurrentState.TickNumber = TickNumber;
        CurrentState.InputPacketTickNumber = CurrentInput.PacketNumber;

        auto* Handle = GetPhysicsHandle();
        if (Handle == nullptr) {
            UE_LOG(LogTemp, Error, TEXT("Tried post-simulate without a valid physics handle"));
            return;
        }

        FPhysicsContext Context(Handle, UpdatedComponent);
        Delegate->SimulatePrePhysics(Dt, Context, CurrentInput.Body, LastState, CurrentState);
    }

    template <typename InputType, typename StateType>
    void FSimulatedModelDriver<InputType, StateType>::PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        const auto Handle = GetPhysicsHandle();
        if (Handle == nullptr) {
            UE_LOG(LogTemp, Error, TEXT("Tried post-simulate without a valid physics handle"));
            return;
        }

        CurrentState.FillState(Handle);
        CurrentState.StartTime = StartTime;
        CurrentState.EndTime = EndTime;

        const FPhysicsContext Context(Handle, UpdatedComponent);
        Delegate->SimulatePostPhysics(Dt, Context, CurrentInput.Body, LastState, CurrentState);

        UpdateHistory(CurrentState);
    }

    template <typename InputType, typename StateType>
    void FSimulatedModelDriver<InputType, StateType>::InterpolateStateGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        StateType InterpolatedState{};
        History.GetStateAtTime(SimTime, InterpolatedState);
        Delegate->Finalize(InterpolatedState, Dt);

        FScopeLock EventQueueLock(&EventQueueMutex);
        while (!EventQueue.IsEmpty()) {
            const FEvent<StateType>& Front = EventQueue[0];
            if (Front.State.StartTime > SimTime) { break; }

            Delegate->DispatchEvents(Front.State, Front.Events);
            EventQueue.RemoveAt(0);
        }
    }

    template <typename InputType, typename StateType>
    void FSimulatedModelDriver<InputType, StateType>::UpdateHistory(const FStateWrapper<StateType>& State) {
        FScopeLock EventQueueLock(&EventQueueMutex);

        FStateWrapper<StateType> PreviousHistoryState{};
        bool bEventQueueIsDirty = false;

        if (History.GetStateAtTick(State.TickNumber, PreviousHistoryState)) {
            const uint8 DirtyEvents = State.Events & (~PreviousHistoryState.Events);
            if (DirtyEvents != 0) {
                EventQueue.Add({State, DirtyEvents});
                bEventQueueIsDirty = true;
            }
        }
        else if (State.Events != 0) {
            EventQueue.Add({State, State.Events});
            bEventQueueIsDirty = true;
        }

        if (bEventQueueIsDirty) {
            EventQueue.Sort([](const auto& A, const auto& B) { return A.State.StartTime < B.State.StartTime; });
        }

        History.Update(State);
    }
}
