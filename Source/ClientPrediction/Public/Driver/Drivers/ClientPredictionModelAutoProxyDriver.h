#pragma once

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/Input/ClientPredictionAutoProxyInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "Driver/Input/ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"
#include "World/ClientPredictionWorldManager.h"

namespace ClientPrediction {
    template <typename InputType, typename StateType>
    class FModelAutoProxyDriver final : public FSimulatedModelDriver<InputType, StateType>, public IRewindCallback, public StateConsumerBase<FStateWrapper<StateType>> {
    public:
        FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FRepProxy& ControlProxyRep,
                              int32 RewindBufferSize);

        virtual ~FModelAutoProxyDriver() override = default;

        virtual void Register(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;
        virtual void Unregister(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;

    private:
        void BindToRepProxy(FRepProxy& ControlProxyRep);

    public:
        virtual void ConsumeUnserializedStateForTick(const int32 Tick, const FStateWrapper<StateType>& State, const Chaos::FReal ServerTime) override;

    private:
        void QueueAuthorityState(const FStateWrapper<StateType>& State);

    public:
        // Ticking
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;
        void ApplyCorrectionIfNeeded(int32 TickNumber);

        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    private:
        void EmitInputs();

    public:
        virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, const class Chaos::FRewindData& RewindData) override;

    private:
        FAutoProxyInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread
        int32 RewindBufferSize = 0;

        FStateWrapper<StateType> PendingCorrection{}; // Only used on physics thread
        int32 PendingPhysicsCorrectionFrame = INDEX_NONE; // Only used on physics thread

        FCriticalSection PendingAuthorityStatesMutex;
        TArray<FStateWrapper<StateType>> PendingAuthorityStates; // Written to from the game thread, read by the physics thread
        int32 LastAckedTick = INDEX_NONE; // Only used on the physics thread but might be used on the game thread later

        FCriticalSection InputSendMutex;
        int32 LastModifiedInputPacket = INDEX_NONE; // Only used on physics thread
        TQueue<FInputPacketWrapper<InputType>> InputSendQueue; // Written to from the physics, read by the game thread
        TArray<FInputPacketWrapper<InputType>> InputSlidingWindow; // Only used on game thread

        FControlPacket LastControlPacket{};

        FStateManager* StateManager = nullptr;
    };

    template <typename InputType, typename StateType>
    FModelAutoProxyDriver<InputType, StateType>::FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                                                                       FRepProxy& ControlProxyRep, int32 RewindBufferSize) :
        FSimulatedModelDriver(UpdatedComponent, Delegate, RewindBufferSize), RewindBufferSize(RewindBufferSize) {
        BindToRepProxy(ControlProxyRep);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::Register(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        FSimulatedModelDriver<InputType, StateType>::Register(WorldManager, ModelId);
        WorldManager->GetStateManager().RegisterConsumerForModel(ModelId, this);
        WorldManager->AddRewindCallback(this);

        StateManager = &WorldManager->GetStateManager();
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::Unregister(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        FSimulatedModelDriver<InputType, StateType>::Unregister(WorldManager, ModelId);
        WorldManager->GetStateManager().UnregisterConsumerForModel(ModelId);
        WorldManager->RemoveRewindCallback(this);

        StateManager = nullptr;
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::BindToRepProxy(FRepProxy& ControlProxyRep) {
        ControlProxyRep.SerializeFunc = [&](FArchive& Ar) {
            LastControlPacket.NetSerialize(Ar);
        };
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::ConsumeUnserializedStateForTick(const int32 Tick, const FStateWrapper<StateType>& State,
                                                                                      const Chaos::FReal ServerTime) {
        QueueAuthorityState(State);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::QueueAuthorityState(const FStateWrapper<StateType>& State) {
        FScopeLock Lock(&PendingAuthorityStatesMutex);

        const int32 AuthLocalTickNumber = State.InputPacketTickNumber;
        if (AuthLocalTickNumber <= LastAckedTick) { return; }

        const bool bStateAlreadyPending = PendingAuthorityStates.ContainsByPredicate([&](const auto& Candidate) {
            return Candidate.TickNumber == State.TickNumber;
        });

        if (!bStateAlreadyPending) {
            PendingAuthorityStates.Add(State);
            PendingAuthorityStates.Sort([](const auto& A, const auto& B) {
                return A.TickNumber < B.TickNumber;
            });
        }
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
        FInputPacketWrapper<InputType> Packet;
        Packet.PacketNumber = TickNumber;
        Delegate->ProduceInput(Packet.Body);

        InputBuf.QueueInputPacket(Packet);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        ApplyCorrectionIfNeeded(TickNumber);

        // This provides mutable pointer to the input in the input buffer so that any modifications made in the next step will also update the
        // input stored in the input buffer
        FInputPacketWrapper<InputType>* BufferedInput = InputBuf.InputForTick(TickNumber);
        check(BufferedInput)

        // We guard against modifying packets older than LastModifiedInputPacket, so that they can't be re-modified during a resim.
        // Once the packet has been modified once, it is considered final and can be sent to the authority.
        if (LastModifiedInputPacket < BufferedInput->PacketNumber) {
            Delegate->ModifyInputPhysicsThread(BufferedInput->Body, CurrentState, Dt);
            LastModifiedInputPacket = BufferedInput->PacketNumber;

            FScopeLock Lock(&InputSendMutex);
            InputSendQueue.Enqueue(*BufferedInput);
        }

        CurrentInput = *BufferedInput;
        PreTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::ApplyCorrectionIfNeeded(const int32 TickNumber) {
        if (PendingPhysicsCorrectionFrame == INDEX_NONE) { return; }
        check(PendingPhysicsCorrectionFrame == TickNumber);

        auto* PhysicsHandle = GetPhysicsHandle();
        PendingCorrection.Reconcile(PhysicsHandle);
        PendingPhysicsCorrectionFrame = INDEX_NONE;

        CurrentState = PendingCorrection;
        UpdateHistory(CurrentState);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        PostTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        History.UpdateTimeWaitingToBeConsumed(Dt);
        EmitInputs();

        InterpolateStateGameThread(SimTime, Dt);

        // If the input buffer is really unhealthy, the client needs to catch up quickly
        Chaos::FReal NewTimeDilation = 1.0 + LastControlPacket.GetTimeDilation() * Settings->MaxAutoProxyTimeDilation;

        if (LastControlPacket.bIsInputBufferVeryUnhealthy) {
            UE_LOG(LogTemp, Warning, TEXT("Input buffer is very unhealthy, so the auto proxy will be sped up significantly"));
            NewTimeDilation = Settings->AutoProxyMaxSpeedupTimescale;
        }

        {
            FScopeLock Lock(&PendingAuthorityStatesMutex);
            if (!PendingAuthorityStates.IsEmpty()) {
                const int32 CurrentTickNumber = History.GetLatestTickNumber();

                // If the auto proxy is far ahead of the authority, slow down time significantly so the authority can catch up
                if (PendingAuthorityStates.Last().InputPacketTickNumber <= CurrentTickNumber - RewindBufferSize) {
                    UE_LOG(LogTemp, Warning, TEXT("Auto proxy is too far ahead of the authority"));
                    NewTimeDilation = Settings->AutoProxyAuthorityCatchupTimescale;
                }
            }
        }

        Delegate->SetTimeDilation(NewTimeDilation);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::EmitInputs() {
        FScopeLock Lock(&InputSendMutex);

        const float EstimatedDisplayedServerTick = StateManager != nullptr ? StateManager->GetEstimatedCurrentServerTick() : INDEX_NONE;
        float EstimatedOffsetFromWaitingInBuffer = History.GetAverageTimeToConsumeState() / Settings->FixedDt;

        while (!InputSendQueue.IsEmpty()) {
            FInputPacketWrapper<InputType> Packet;
            InputSendQueue.Peek(Packet);
            InputSendQueue.Pop();

            // Once a state is produced by the simulation, it is added to the history queue. The game thread will then interpolate between the produced states.
            // Events are not actually dispatched until the simulation time has passed the start time of the state, so there is some time that needs to be accounted for.
            // We can't know the true time (since that is something that is going to happen in the future), so we just use the current rolling average for time in the
            // buffer. If more than one input packets were produced during this update, each successive tick will spend about 1 tick of time longer in the history
            // queue, so we increment the EstimatedOffsetFromWaitingInBuffer.
            Packet.EstimatedDisplayedServerTick = EstimatedDisplayedServerTick + EstimatedOffsetFromWaitingInBuffer++;

            // Bundle the new packet up with the most recent inputs and send it to the authority
            InputSlidingWindow.Add(Packet);
            while (InputSlidingWindow.Num() > Settings->InputSlidingWindowSize) {
                InputSlidingWindow.RemoveAt(0);
            }

            Delegate->EmitInputPackets(InputSlidingWindow);
        }
    }

    template <typename InputType, typename StateType>
    int32 FModelAutoProxyDriver<InputType, StateType>::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
        FScopeLock Lock(&PendingAuthorityStatesMutex);

        // If the latest authority state is too old to reconcile with, just don't rewind. This will be handled by stopping the auto proxy
        // to allow the authority to catch up.
        if (!PendingAuthorityStates.IsEmpty() && PendingAuthorityStates.Last().InputPacketTickNumber <= CurrentTickNumber - RewindBufferSize) {
            return INDEX_NONE;
        }

        while (!PendingAuthorityStates.IsEmpty()) {
            const int32 AuthLocalTickNumber = PendingAuthorityStates[0].InputPacketTickNumber;

            // The auto proxy has fallen behind the authority, so there is no need to reconcile with it right now, since we can't predict ahead
            if (AuthLocalTickNumber > CurrentTickNumber) { return INDEX_NONE; }

            // The state will be processed, it can be popped from the queue
            FStateWrapper<StateType> AuthState = PendingAuthorityStates[0];
            PendingAuthorityStates.RemoveAt(0);

            const bool bStateHasInvalidTickNumber = AuthLocalTickNumber == INDEX_NONE;
            const bool bStateHasAlreadyBeenAcked = AuthLocalTickNumber <= LastAckedTick;
            const bool bStateIsTooOld = AuthLocalTickNumber <= CurrentTickNumber - RewindBufferSize;

            if (bStateHasInvalidTickNumber || bStateHasAlreadyBeenAcked || bStateIsTooOld) { continue; }

            LastAckedTick = AuthLocalTickNumber;
            InputBuf.Ack(AuthLocalTickNumber);

            // Check against the historic state
            FStateWrapper<StateType> HistoricState;
            check(History.GetStateAtTick(AuthLocalTickNumber, HistoricState));
            check(HistoricState.TickNumber == AuthLocalTickNumber)

            if (AuthState.ShouldReconcile(HistoricState)) {
                UE_LOG(LogTemp, Error, TEXT("Rewinding and rolling back to %d"), AuthLocalTickNumber + 1);

                // When we perform a correction, we add one to the frame, since LastAuthorityState will be the state
                // of the simulation during PostTickPhysicsThread (after physics has been simulated), so it is the beginning
                // state for LocalTickNumber + 1
                PendingPhysicsCorrectionFrame = AuthLocalTickNumber + 1;

                PendingCorrection = AuthState;
                PendingCorrection.StartTime = HistoricState.StartTime;
                PendingCorrection.EndTime = HistoricState.EndTime;
                PendingCorrection.TickNumber = HistoricState.TickNumber;

                // TODO this needs some work, since during a resim the events might not be dirty and won't get re-added to the queue.
                // Remove events for ticks that will be re-simulated, since their state / events might be different
                FScopeLock EventQueueLock(&EventQueueMutex);
                while (!EventQueue.IsEmpty() && EventQueue.Last().State.TickNumber >= PendingCorrection.TickNumber) {
                    EventQueue.RemoveAt(EventQueue.Num() - 1);
                }

                return AuthLocalTickNumber + 1;
            }
        }

        return INDEX_NONE;
    }
}
