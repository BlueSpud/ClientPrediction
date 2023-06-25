#pragma once

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "ClientPredictionModelTypes.h"
#include "Driver/Input/ClientPredictionAuthInputBuf.h"
#include "Driver/Drivers/ClientPredictionSimulatedDriver.h"
#include "Driver/Input/ClientPredictionInput.h"

namespace ClientPrediction {
    template <typename InputType, typename StateType>
    class FModelAuthDriver final : public FSimulatedModelDriver<InputType, StateType>, public StateProducerBase<FStateWrapper<StateType>> {
    public:
        FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FRepProxy& ControlProxyRep,
                         int32 RewindBufferSize, const bool bTakesInput);

        virtual ~FModelAuthDriver() override = default;

        virtual void Register(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;
        virtual void Unregister(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;

    public:
        // Ticking
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;

        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

        // StateProducerBase
        virtual bool ProduceUnserializedStateForTick(const int32 Tick, FStateWrapper<StateType>& State, bool& bShouldBeReliable) override;

    private:
        void SuggestTimeDilation();

    public:
        // Called on game thread
        virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) override;

    private:
        FRepProxy& ControlProxyRep;
        bool bTakesInput = false;

        FAuthInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread
        Chaos::FReal LastSuggestedTimeDilation = 1.0; // Only used on game thread
    };

    template <typename InputType, typename StateType>
    FModelAuthDriver<InputType, StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                                                             FRepProxy& ControlProxyRep, int32 RewindBufferSize, const bool bTakesInput)
        : FSimulatedModelDriver(UpdatedComponent, Delegate, RewindBufferSize), ControlProxyRep(ControlProxyRep),
          bTakesInput(bTakesInput), InputBuf(Settings, bTakesInput) {}

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::Register(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        FSimulatedModelDriver<InputType, StateType>::Register(WorldManager, ModelId);
        WorldManager->GetStateManager().RegisterProducerForModel(ModelId, this);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::Unregister(FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) {
        FSimulatedModelDriver<InputType, StateType>::Unregister(WorldManager, ModelId);
        WorldManager->GetStateManager().UnregisterProducerForModel(ModelId);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {
        if (bTakesInput) {
            FInputPacketWrapper<InputType> Packet;
            Packet.PacketNumber = TickNumber;
            Delegate->ProduceInput(Packet.Body);

            InputBuf.QueueInputPackets({Packet}, 0.0);
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        if (CurrentInput.PacketNumber != INDEX_NONE || InputBuf.GetBufferSize() >= static_cast<uint32>(Settings->DesiredInputBufferSize) || bTakesInput) {
            InputBuf.GetNextInputPacket(CurrentInput, Dt);
        }

        if (bTakesInput) { Delegate->ModifyInputPhysicsThread(CurrentInput.Body, CurrentState, Dt); }
        PreTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        PostTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        InterpolateStateGameThread(SimTime, Dt);
        SuggestTimeDilation();
    }

    template <typename InputType, typename StateType>
    bool FModelAuthDriver<InputType, StateType>::ProduceUnserializedStateForTick(const int32 Tick, FStateWrapper<StateType>& State, bool& bShouldBeReliable) {
        State = CurrentState;
        bShouldBeReliable = State.Events != 0;
        return true;
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::SuggestTimeDilation() {
        const uint16 InputBufferSize = InputBuf.GetBufferSize();
        const uint16 DesiredInputBufferSize = Settings->DesiredInputBufferSize + InputBuf.GetNumRecentlyDroppedInputPackets();

        const Chaos::FReal TargetTimeDilation = InputBufferSize > DesiredInputBufferSize ? -1.0 : (InputBufferSize < DesiredInputBufferSize ? 1.0 : 0.0);
        LastSuggestedTimeDilation = FMath::Lerp(LastSuggestedTimeDilation, TargetTimeDilation, Settings->TimeDilationAlpha);

        FControlPacket ControlPacket{};
        ControlPacket.SetTimeDilation(LastSuggestedTimeDilation);

        ControlProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { ControlPacket.NetSerialize(Ar); };
        ControlProxyRep.Dispatch();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {
        FNetworkConditions NetworkConditions{};
        Delegate->GetNetworkConditions(NetworkConditions);

        InputBuf.QueueInputPackets(Packets, NetworkConditions.Latency);
    }
}
