#pragma once

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "ClientPredictionModelTypes.h"
#include "Driver/Input/ClientPredictionAuthInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/Input/ClientPredictionInput.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize;
    extern CLIENTPREDICTION_API int32 ClientPredictionDroppedPacketMemoryTickLength;
    extern CLIENTPREDICTION_API float ClientPredictionTimeDilationAlpha;

    template <typename InputType, typename StateType>
    class FModelAuthDriver final : public FSimulatedModelDriver<InputType, StateType> {
    public:
        FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                         FRepProxy& AutoProxyRep, FRepProxy& SimProxyRep, FRepProxy& ControlProxyRep,
                         int32 RewindBufferSize, const bool bTakesInput);

        virtual ~FModelAuthDriver() override = default;

    public:
        // Ticking
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;

        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    private:
        void SendCurrentStateToRemotes();
        void SuggestTimeDilation();

    public:
        // Called on game thread
        virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) override;

    private:
        FRepProxy& AutoProxyRep;
        FRepProxy& SimProxyRep;
        FRepProxy& ControlProxyRep;
        bool bTakesInput = false;

        FAuthInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread
        Chaos::FReal LastSuggestedTimeDilation = 1.0; // Only used on game thread

        FCriticalSection LastStateGtMutex;
        FStateWrapper<StateType> LastStateGt; // Written from physics thread, read on game thread
        int32 LastEmittedState = INDEX_NONE; // Only used on game thread
    };

    template <typename InputType, typename StateType>
    FModelAuthDriver<InputType, StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent,
                                                             IModelDriverDelegate<InputType, StateType>* Delegate,
                                                             FRepProxy& AutoProxyRep, FRepProxy& SimProxyRep,
                                                             FRepProxy& ControlProxyRep, int32 RewindBufferSize, const bool bTakesInput)
        : FSimulatedModelDriver(UpdatedComponent, Delegate, RewindBufferSize), AutoProxyRep(AutoProxyRep),
          SimProxyRep(SimProxyRep), ControlProxyRep(ControlProxyRep), bTakesInput(bTakesInput), InputBuf(ClientPredictionDroppedPacketMemoryTickLength) {}

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
        if (bTakesInput) {
            FInputPacketWrapper<InputType> Packet;
            Packet.PacketNumber = TickNumber;
            Delegate->ProduceInput(Packet);

            InputBuf.QueueInputPackets({Packet});
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        if (CurrentInput.PacketNumber != INDEX_NONE || InputBuf.GetBufferSize() >= static_cast<uint32>(ClientPredictionDesiredInputBufferSize) || bTakesInput) {
            InputBuf.GetNextInputPacket(CurrentInput);
        }

        PreTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        PostTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);

        FScopeLock Lock(&LastStateGtMutex);
        LastStateGt = CurrentState;
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        InterpolateStateGameThread(SimTime, Dt);
        SendCurrentStateToRemotes();
        SuggestTimeDilation();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::SendCurrentStateToRemotes() {
        FStateWrapper<StateType> SendingState = LastStateGt;
        {
            FScopeLock Lock(&LastStateGtMutex);
            SendingState = LastStateGt;
        }

        if (SendingState.TickNumber != INDEX_NONE && SendingState.TickNumber != LastEmittedState) {
            AutoProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };
            SimProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { SendingState.NetSerialize(Ar); };

            AutoProxyRep.Dispatch();
            SimProxyRep.Dispatch();

            LastEmittedState = SendingState.TickNumber;
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::SuggestTimeDilation() {
        const uint16 InputBufferSize = InputBuf.GetBufferSize();
        const uint16 DesiredInputBufferSize = ClientPredictionDesiredInputBufferSize + InputBuf.GetNumRecentlyDroppedInputPackets();

        const Chaos::FReal TargetTimeDilation = InputBufferSize > DesiredInputBufferSize ? -1.0 : (InputBufferSize < DesiredInputBufferSize ? 1.0 : 0.0);
        LastSuggestedTimeDilation = FMath::Lerp(LastSuggestedTimeDilation, TargetTimeDilation, ClientPredictionTimeDilationAlpha);

        FControlPacket ControlPacket{};
        ControlPacket.SetTimeDilation(LastSuggestedTimeDilation);

        ControlProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { ControlPacket.NetSerialize(Ar); };
        ControlProxyRep.Dispatch();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(
        const TArray<FInputPacketWrapper<InputType>>& Packets) {
        InputBuf.QueueInputPackets(Packets);
    }
}
