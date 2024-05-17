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
        FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate, FRepProxy& ControlRepProxy,
                         FRepProxy& FinalStateRepProxy, int32 RewindBufferSize, const bool bTakesInput);

        virtual ~FModelAuthDriver() override = default;

        virtual void Register(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;
        virtual void Unregister(struct FWorldManager* WorldManager, const FClientPredictionModelId& ModelId) override;

    public:
        // Ticking
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) override;
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) override;
        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) override;

        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) override;

    protected:
        virtual void HandleSimulationEndGameThread() override;

    public:
        // StateProducerBase
        virtual bool ProduceUnserializedStateForTick(const int32 Tick, FStateWrapper<StateType>& State, bool& bShouldBeReliable) override;

    private:
        void SuggestTimeDilation();

    public:
        // Called on game thread
        virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) override;

    private:
        FRepProxy& ControlRepProxy;
        FRepProxy& FinalStateRepProxy;

        bool bTakesInput = false;

        FAuthInputBuf<InputType> InputBuf; // Written to on game thread, read from physics thread
        Chaos::FReal LastSuggestedTimeDilation = 1.0; // Only used on game thread
    };

    template <typename InputType, typename StateType>
    FModelAuthDriver<InputType, StateType>::FModelAuthDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate<InputType, StateType>* Delegate,
                                                             FRepProxy& ControlRepProxy, FRepProxy& FinalStateRepProxy, int32 RewindBufferSize, const bool bTakesInput)
        : FSimulatedModelDriver<InputType, StateType>(UpdatedComponent, Delegate, RewindBufferSize), ControlRepProxy(ControlRepProxy),
          FinalStateRepProxy(FinalStateRepProxy),
          bTakesInput(bTakesInput), InputBuf(FSimulatedModelDriver<InputType, StateType>::Settings, bTakesInput) {}

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
        if (FSimulatedModelDriver<InputType, StateType>::HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        if (bTakesInput) {
            FInputPacketWrapper<InputType> Packet;
            Packet.PacketNumber = TickNumber;
            FSimulatedModelDriver<InputType, StateType>::Delegate->ProduceInput(Packet.Body);

            InputBuf.QueueInputPackets({Packet});
        }
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {
        if (FSimulatedModelDriver<InputType, StateType>::HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        if (FSimulatedModelDriver<InputType, StateType>::CurrentInput.PacketNumber != INDEX_NONE || InputBuf.GetBufferSize() >= static_cast<uint32>(FSimulatedModelDriver<
            InputType, StateType>::Settings->DesiredInputBufferSize) || bTakesInput) {
            InputBuf.GetNextInputPacket(FSimulatedModelDriver<InputType, StateType>::CurrentInput, Dt);
        }

        if (bTakesInput) {
            FSimulatedModelDriver<InputType, StateType>::Delegate->ModifyInputPhysicsThread(FSimulatedModelDriver<InputType, StateType>::CurrentInput.Body,
                                                                                            FSimulatedModelDriver<InputType, StateType>::CurrentState, Dt);
        }
        FSimulatedModelDriver<InputType, StateType>::PreTickSimulateWithCurrentInput(TickNumber, Dt, StartTime, EndTime);

        if (FSimulatedModelDriver<InputType, StateType>::Delegate->IsSimulationOver(FSimulatedModelDriver<InputType, StateType>::CurrentState.Body)) {
            FSimulatedModelDriver<InputType, StateType>::CurrentState.bIsFinalState = true;
            FSimulatedModelDriver<InputType, StateType>::FinalTick = TickNumber;

            if (auto* Handle = FSimulatedModelDriver<InputType, StateType>::GetPhysicsHandle()) {
                Handle->SetObjectState(Chaos::EObjectStateType::Kinematic);
                Handle->SetV(Chaos::FVec3::ZeroVector);
                Handle->SetW(Chaos::FVec3::ZeroVector);
            }
        }

        if (bTakesInput || FSimulatedModelDriver<InputType, StateType>::CurrentInput.EstimatedDisplayedServerTick == static_cast<float>(INDEX_NONE)) {
            return;
        }

        // If this value is in the future, it means that the client is running too far ahead of the server and will eventually be slowed down,
        // or it means that the client is cheating.
        if (FSimulatedModelDriver<InputType, StateType>::CurrentInput.EstimatedDisplayedServerTick >= TickNumber) {
            FSimulatedModelDriver<InputType, StateType>::CurrentState.EstimatedAutoProxyDelay = 0.0;
            return;
        }

        const int32 TickDelta = TickNumber - FSimulatedModelDriver<InputType, StateType>::CurrentInput.EstimatedDisplayedServerTick;
        FSimulatedModelDriver<InputType, StateType>::CurrentState.EstimatedAutoProxyDelay = static_cast<Chaos::FReal>(TickDelta) * FSimulatedModelDriver<
            InputType, StateType>::Settings->FixedDt;
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {
        if (FSimulatedModelDriver<InputType, StateType>::HasSimulationEndedOnPhysicsThread(TickNumber)) { return; }

        FSimulatedModelDriver<InputType, StateType>::PostTickSimulateWithCurrentInput(TickNumber, Dt);
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {
        if (FSimulatedModelDriver<InputType, StateType>::bHasSimulationEndedGameThread) { return; }

        FSimulatedModelDriver<InputType, StateType>::InterpolateStateGameThread(SimTime, Dt);

        if (FSimulatedModelDriver<InputType, StateType>::bHasSimulationEndedGameThread) {
            HandleSimulationEndGameThread();
            return;
        }

        SuggestTimeDilation();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::HandleSimulationEndGameThread() {
        FStateWrapper<StateType> FinalState;
        FSimulatedModelDriver<InputType, StateType>::History.GetStateAtTick(FSimulatedModelDriver<InputType, StateType>::FinalTick, FinalState);

        FinalStateRepProxy.SerializeFunc = [=](FArchive& Ar) mutable { FinalState.NetSerialize(Ar, EDataCompleteness::kFull); };
        FinalStateRepProxy.Dispatch();

        FSimulatedModelDriver<InputType, StateType>::HandleSimulationEndGameThread();
    }

    template <typename InputType, typename StateType>
    bool FModelAuthDriver<InputType, StateType>::ProduceUnserializedStateForTick(const int32 Tick, FStateWrapper<StateType>& State, bool& bShouldBeReliable) {
        if (FSimulatedModelDriver<InputType, StateType>::HasSimulationEndedOnPhysicsThread(Tick)) { return false; }

        State = FSimulatedModelDriver<InputType, StateType>::CurrentState;
        bShouldBeReliable = State.Events != 0 || State.bIsFinalState;

        return true;
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::SuggestTimeDilation() {
        const int16 InputBufferSize = InputBuf.GetBufferSize();
        const int16 AdjustedDesiredInputBufferSize = FSimulatedModelDriver<InputType, StateType>::Settings->DesiredInputBufferSize + InputBuf.
            GetNumRecentlyDroppedInputPackets();

        const int16 DeltaFromTarget = InputBufferSize - AdjustedDesiredInputBufferSize;
        const Chaos::FReal DeltaFromTargetPercentage = static_cast<Chaos::FReal>(DeltaFromTarget) / static_cast<Chaos::FReal>(FSimulatedModelDriver<
            InputType, StateType>::Settings->DesiredInputBufferSize);
        const Chaos::FReal TargetTimeDilation = -FMath::Sign(DeltaFromTarget);

        LastSuggestedTimeDilation = FMath::Lerp(LastSuggestedTimeDilation, TargetTimeDilation, FSimulatedModelDriver<InputType, StateType>::Settings->TimeDilationAlpha);

        FControlPacket ControlPacket{};
        ControlPacket.SetTimeDilation(LastSuggestedTimeDilation);
        ControlPacket.bIsInputBufferVeryUnhealthy = DeltaFromTargetPercentage < -FSimulatedModelDriver<InputType, StateType>::Settings->UnhealthyInputBufferPercentage;

        ControlRepProxy.SerializeFunc = [=](FArchive& Ar) mutable { ControlPacket.NetSerialize(Ar); };
        ControlRepProxy.Dispatch();
    }

    template <typename InputType, typename StateType>
    void FModelAuthDriver<InputType, StateType>::ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {
        InputBuf.QueueInputPackets(Packets);
    }
}
