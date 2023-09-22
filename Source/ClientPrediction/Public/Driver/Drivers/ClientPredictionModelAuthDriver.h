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
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;
        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;

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
        if (HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        if (bTakesInput) {
            FInputPacketWrapper<InputType> Packet;
            Packet.PacketNumber = TickNumber;
            Delegate->ProduceInput(Packet.Body);

            InputBuf.QueueInputPackets({Packet});
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        if (HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        if (CurrentInput.PacketNumber != INDEX_NONE || InputBuf.GetBufferSize() >= static_cast<uint32>(Settings->DesiredInputBufferSize) || bTakesInput) {
            InputBuf.GetNextInputPacket(CurrentInput, Dt);
        }

        if (bTakesInput) { Delegate->ModifyInputPhysicsThread(CurrentInput.Body, CurrentState, Dt); }
        PreTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);

        if (bTakesInput || CurrentInput.EstimatedDisplayedServerTick == INDEX_NONE) {
            return;
        }

        // If this value is in the future, it means that the client is running too far ahead of the server and will eventually be slowed down,
        // or it means that the client is cheating.
        if (CurrentInput.EstimatedDisplayedServerTick >= TickNumber) {
            CurrentState.EstimatedAutoProxyDelay = 0.0;
            return;
        }

        const int32 TickDelta = TickNumber - CurrentInput.EstimatedDisplayedServerTick;
        CurrentState.EstimatedAutoProxyDelay = static_cast<Chaos::FReal>(TickDelta) * Settings->FixedDt;

        if (Delegate->IsSimulationOver(CurrentState.Body)) {
            CurrentState.bIsFinalState = true;
            FinalTick = TickNumber;

            if (auto* Handle = GetPhysicsHandle()) {
                Handle->SetObjectState(Chaos::EObjectStateType::Static);
            }
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        if (HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        PostTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        if (bHasSimulationEndedGameThread) { return; }

        InterpolateStateGameThread(SimTime, Dt);

        if (bHasSimulationEndedGameThread) {
            HandleSimulationEndGameThread();
            return;
        }

        SuggestTimeDilation();
    }

    template <typename InputType, typename StateType>
    bool FModelAuthDriver<InputType, StateType>::ProduceUnserializedStateForTick(const int32 Tick, FStateWrapper<StateType>& State, bool& bShouldBeReliable) {
        if (HasSimulationEndedOnPhysicsThread(Tick)) { return false; }

        State = CurrentState;
        bShouldBeReliable = State.Events != 0 || State.bIsFinalState;

        return true;
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::SuggestTimeDilation() {
        const int16 InputBufferSize = InputBuf.GetBufferSize();
        const int16 AdjustedDesiredInputBufferSize = Settings->DesiredInputBufferSize + InputBuf.GetNumRecentlyDroppedInputPackets();

        const int16 DeltaFromTarget = InputBufferSize - AdjustedDesiredInputBufferSize;
        const Chaos::FReal DeltaFromTargetPercentage = static_cast<Chaos::FReal>(DeltaFromTarget) / static_cast<Chaos::FReal>(Settings->DesiredInputBufferSize);
        const Chaos::FReal TargetTimeDilation = -FMath::Sign(DeltaFromTarget);

        LastSuggestedTimeDilation = FMath::Lerp(LastSuggestedTimeDilation, TargetTimeDilation, Settings->TimeDilationAlpha);

        FControlPacket ControlPacket{};
        ControlPacket.SetTimeDilation(LastSuggestedTimeDilation);
        ControlPacket.bIsInputBufferVeryUnhealthy = DeltaFromTargetPercentage < -Settings->UnhealthyInputBufferPercentage;

        ControlProxyRep.SerializeFunc = [=](FArchive& Ar) mutable { ControlPacket.NetSerialize(Ar); };
        ControlProxyRep.Dispatch();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {
        InputBuf.QueueInputPackets(Packets);
    }
}
