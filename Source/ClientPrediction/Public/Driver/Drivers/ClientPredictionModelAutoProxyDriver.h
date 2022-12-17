#pragma once

#include <atomic>

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/Input/ClientPredictionAutoProxyInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "Driver/Input/ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize;

    template <typename InputType, typename StateType>
    class FModelAutoProxyDriver final : public FSimulatedModelDriver<InputType, StateType>, public IRewindCallback {
    public:
        FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FRepProxy& AutoProxyRep,
                              FRepProxy& ControlProxyRep, int32 RewindBufferSize);

        virtual ~FModelAutoProxyDriver() override = default;

    private:
        void BindToRepProxy(FRepProxy& AutoProxyRep);

    public:
        // Ticking
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
        void ApplyCorrectionIfNeeded(int32 TickNumber);

        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;
        virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, const class Chaos::FRewindData& RewindData) override;

    private:
        FAutoProxyInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread
        int32 RewindBufferSize = 0;

        FPhysicsState<StateType> PendingCorrection{}; // Only used on physics thread
        int32 PendingPhysicsCorrectionFrame = INDEX_NONE; // Only used on physics thread

        FCriticalSection LastAuthorityMutex;
        FPhysicsState<StateType> LastAuthorityState; // Written to from the game thread, read by the physics thread
        int32 LastAckedTick = INDEX_NONE; // Only used on the physics thread but might be used on the game thread later

        // Game thread
        TArray<FInputPacketWrapper<InputType>> InputSlidingWindow;
    };

    template <typename InputType, typename StateType>
    FModelAutoProxyDriver<InputType, StateType>::FModelAutoProxyDriver(UPrimitiveComponent* UpdatedComponent,
                                                                       IModelDriverDelegate<InputType, StateType>*
                                                                       Delegate,
                                                                       FRepProxy& AutoProxyRep,
                                                                       FRepProxy& ControlProxyRep,
                                                                       int32 RewindBufferSize) :
        FSimulatedModelDriver(UpdatedComponent, Delegate, RewindBufferSize), InputBuf(RewindBufferSize),
        RewindBufferSize(RewindBufferSize) {
        BindToRepProxy(AutoProxyRep);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::BindToRepProxy(FRepProxy& AutoProxyRep) {
        AutoProxyRep.SerializeFunc = [&](FArchive& Ar) {
            FScopeLock Lock(&LastAuthorityMutex);
            LastAuthorityState.NetSerialize(Ar);
        };
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
        // Sample a new input packet
        FInputPacketWrapper<InputType> Packet;
        Packet.PacketNumber = TickNumber;
        Delegate->ProduceInput(Packet);

        InputBuf.QueueInputPacket(Packet);

        // Bundle it up with the most recent inputs and send it to the authority
        InputSlidingWindow.Add(Packet);
        while (InputSlidingWindow.Num() > ClientPredictionInputSlidingWindowSize) {
            InputSlidingWindow.RemoveAt(0);
        }

        Delegate->EmitInputPackets(InputSlidingWindow);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        ApplyCorrectionIfNeeded(TickNumber);

        check(InputBuf.InputForTick(TickNumber, CurrentInput))
        PreTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::ApplyCorrectionIfNeeded(int32 TickNumber) {
        if (PendingPhysicsCorrectionFrame == INDEX_NONE) { return; }
        check(PendingPhysicsCorrectionFrame == TickNumber);

        auto* PhysicsHandle = GetPhysicsHandle();
        PendingCorrection.Reconcile(PhysicsHandle);
        PendingPhysicsCorrectionFrame = INDEX_NONE;

        LastState = PendingCorrection;
    }

    template <typename InputType, typename StateType>
    void FModelAutoProxyDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        PostTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);
        History.Update(CurrentState);
    }

    template <typename InputType, typename StateType>
    int32 FModelAutoProxyDriver<InputType, StateType>::GetRewindTickNumber(int32 CurrentTickNumber, const Chaos::FRewindData& RewindData) {
        FScopeLock Lock(&LastAuthorityMutex);
        const int32 LocalTickNumber = LastAuthorityState.InputPacketTickNumber;

        if (LocalTickNumber == INDEX_NONE) { return INDEX_NONE; }
        if (LocalTickNumber <= LastAckedTick) { return INDEX_NONE; }

        // TODO Both of these cases should be handled gracefully
        check(LocalTickNumber <= CurrentTickNumber)
        check(LocalTickNumber >= CurrentTickNumber - RewindBufferSize)

        // Check against the historic state
        FPhysicsState<StateType> HistoricState;
        History.GetStateAtTick(LocalTickNumber, HistoricState);

        check(HistoricState.TickNumber != INDEX_NONE);
        LastAckedTick = LocalTickNumber;

        if (LastAuthorityState.ShouldReconcile(HistoricState)) {
            // When we perform a correction, we add one to the frame, since LastAuthorityState will be the state
            // of the simulation during PostTickPhysicsThread (after physics has been simulated), so it is the beginning
            // state for LocalTickNumber + 1
            PendingPhysicsCorrectionFrame = LocalTickNumber + 1;
            PendingCorrection = LastAuthorityState;

            UE_LOG(LogTemp, Error, TEXT("Rewinding and rolling back to %d"), LocalTickNumber);
            return LocalTickNumber + 1;
        }

        return INDEX_NONE;
    }
}
