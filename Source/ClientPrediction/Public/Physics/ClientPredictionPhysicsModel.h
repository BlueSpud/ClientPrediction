﻿#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "Driver/ClientPredictionModelDriver.h"

#include "Driver/Drivers/ClientPredictionModelAuthDriver.h"
#include "Driver/Drivers/ClientPredictionModelAutoProxyDriver.h"
#include "Driver/Drivers/ClientPredictionModelSimProxyDriver.h"
#include "World/ClientPredictionWorldManager.h"

namespace ClientPrediction {
    // Delegate
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct IPhysicsModelDelegate {
        virtual ~IPhysicsModelDelegate() = default;

        virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
        virtual void EmitReliableAuthorityState(FNetSerializationProxy& Proxy) = 0;
        virtual void GetNetworkConditions(FNetworkConditions& NetworkConditions) const = 0;
    };

    // Interface
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct FPhysicsModelBase {
        virtual ~FPhysicsModelBase() = default;

        virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) = 0;
        virtual void Cleanup() = 0;

        virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& AutoProxyRep, FRepProxy& SimProxyRep, FRepProxy& ControlProxyRep) = 0;
        virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const = 0;
        virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) const = 0;
    };

    // Sim output
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename StateType, typename EventType>
    struct FSimOutput {
        explicit FSimOutput(FStateWrapper<StateType>& StateWrapper)
            : StateWrapper(StateWrapper) {}

        StateType& State() const { return StateWrapper.Body; }

        void DispatchEvent(EventType Event) {
            check(Event < 8)
            StateWrapper.Events |= 0b1 << Event;
        }

    private:
        FStateWrapper<StateType>& StateWrapper;
    };


    // Model declaration
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename InputType, typename StateType, typename EventType>
    struct FPhysicsModel : public FPhysicsModelBase, public IModelDriverDelegate<InputType, StateType> {
        using SimOutput = FSimOutput<StateType, EventType>;

        /**
         * Simulates the model before physics has been run for this ticket.
         * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
         * game thread need to be properly synchronized.
         */
        virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

        /**
         * Simulates the model after physics has been run for this ticket.
         * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
         * game thread need to be properly synchronized.
         */
        virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& OutState) = 0;

        /** Performs any additional setup for the initial simulation state. */
        virtual void GenerateInitialState(StateType& State) const {};

        // FPhysicsModelBase
        virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) override final;

        virtual ~FPhysicsModel() override = default;
        virtual void Cleanup() override final;

        virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& AutoProxyRep, FRepProxy& SimProxyRep, FRepProxy& ControlProxyRep) override final;
        virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const override final;
        virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) const override final;

        virtual void SetTimeDilation(const Chaos::FReal TimeDilation) override final;
        virtual void ForceSimulate(const uint32 NumTicks) override final;
        virtual Chaos::FReal GetWorldTimeNoDilation() const override final;
        virtual void GetNetworkConditions(FNetworkConditions& NetworkConditions) const override final;

        // IModelDriverDelegate
        virtual void GenerateInitialState(FStateWrapper<StateType>& State) override final;
        virtual void Finalize(const StateType& State, Chaos::FReal Dt) override final;

        virtual void EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) override final;
        virtual void EmitReliableAuthorityState(FStateWrapper<StateType> State) override final;
        virtual void ProduceInput(FInputPacketWrapper<InputType>& Packet) override final;

        virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                        FStateWrapper<StateType>& OutState) override final;

        virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                         FStateWrapper<StateType>& OutState) override final;

        virtual void DispatchEvents(const FStateWrapper<StateType>& State, uint8 Events) override final;

    public:
        DECLARE_DELEGATE_OneParam(FPhysicsModelProduceInput, InputType&)
        FPhysicsModelProduceInput ProduceInputDelegate;

        DECLARE_DELEGATE_TwoParams(FPhysicsModelFinalize, const StateType&, Chaos::FReal Dt)
        FPhysicsModelFinalize FinalizeDelegate;

        DECLARE_DELEGATE_ThreeParams(FPhysicsModelDispatchEvent, EventType, const StateType& State, const FPhysicsState& PhysState)
        FPhysicsModelDispatchEvent DispatchEventDelegate;

    private:
        class UPrimitiveComponent* CachedComponent = nullptr;
        struct FWorldManager* CachedWorldManager = nullptr;
        TUniquePtr<IModelDriver<InputType, StateType>> ModelDriver = nullptr;
        IPhysicsModelDelegate* Delegate = nullptr;
    };

    // Implementation
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
        CachedComponent = Component;
        check(CachedComponent);

        Delegate = InDelegate;
        check(Delegate);

        const UWorld* World = CachedComponent->GetWorld();
        check(World);

        CachedWorldManager = FWorldManager::ManagerForWorld(World);
        check(CachedWorldManager)
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::Finalize(const StateType& State, Chaos::FReal Dt) {
        FinalizeDelegate.ExecuteIfBound(State, Dt);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::Cleanup() {
        if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
            CachedWorldManager->RemoveCallback(ModelDriver.Get());
        }

        CachedWorldManager = nullptr;
        ModelDriver = nullptr;
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& AutoProxyRep, FRepProxy& SimProxyRep,
                                                                    FRepProxy& ControlProxyRep) {
        check(CachedWorldManager)
        check(CachedComponent)
        check(Delegate)

        if (ModelDriver != nullptr) {
            CachedWorldManager->RemoveCallback(ModelDriver.Get());
        }

        int32 RewindBufferSize = CachedWorldManager->GetRewindBufferSize();
        switch (Role) {
        case ROLE_Authority:
            ModelDriver = MakeUnique<FModelAuthDriver<InputType, StateType>>(CachedComponent, this, AutoProxyRep, SimProxyRep, ControlProxyRep, RewindBufferSize,
                                                                             bShouldTakeInput);
            break;
        case ROLE_AutonomousProxy: {
            auto NewDriver = MakeUnique<FModelAutoProxyDriver<InputType, StateType>>(CachedComponent, this, AutoProxyRep, ControlProxyRep, RewindBufferSize);
            CachedWorldManager->AddRewindCallback(NewDriver.Get());
            ModelDriver = MoveTemp(NewDriver);
        }
        break;
        case ROLE_SimulatedProxy:
            ModelDriver = MakeUnique<FModelSimProxyDriver<InputType, StateType>>(CachedComponent, this, SimProxyRep);
            break;
        default:
            break;
        }

        CachedWorldManager->AddTickCallback(ModelDriver.Get());
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ReceiveInputPackets(FNetSerializationProxy& Proxy) const {
        if (ModelDriver == nullptr) { return; }

        TArray<FInputPacketWrapper<InputType>> Packets;
        Proxy.NetSerializeFunc = [&](FArchive& Ar) { Ar << Packets; };

        Proxy.Deserialize();
        ModelDriver->ReceiveInputPackets(Packets);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) const {
        if (ModelDriver == nullptr) { return; }

        FStateWrapper<StateType> State;
        Proxy.NetSerializeFunc = [&](FArchive& Ar) { State.NetSerialize(Ar, true); };

        Proxy.Deserialize();
        ModelDriver->ReceiveReliableAuthorityState(State);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::GenerateInitialState(FStateWrapper<StateType>& State) {
        State = {};

        check(CachedComponent)
        const auto& ActorHandle = CachedComponent->BodyInstance.GetPhysicsActorHandle();

        if (ActorHandle != nullptr) {
            const auto& Handle = ActorHandle->GetGameThreadAPI();

            State.PhysicsState.ObjectState = Handle.ObjectState();

            State.PhysicsState.X = Handle.X();
            State.PhysicsState.V = Handle.V();
            State.PhysicsState.R = Handle.R();
            State.PhysicsState.W = Handle.W();
        }

        GenerateInitialState(State.Body);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) {
        check(Delegate);

        FNetSerializationProxy Proxy;
        Proxy.NetSerializeFunc = [=](FArchive& Ar) mutable { Ar << Packets; };
        Delegate->EmitInputPackets(Proxy);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::EmitReliableAuthorityState(FStateWrapper<StateType> State) {
        check(Delegate);

        FNetSerializationProxy Proxy;
        Proxy.NetSerializeFunc = [=](FArchive& Ar) mutable { State.NetSerialize(Ar, true); };
        Delegate->EmitReliableAuthorityState(Proxy);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SetTimeDilation(const Chaos::FReal TimeDilation) {
        check(CachedWorldManager)
        CachedWorldManager->SetTimeDilation(TimeDilation);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ForceSimulate(const uint32 NumTicks) {
        check(CachedWorldManager)
        CachedWorldManager->ForceSimulate(NumTicks);
    }

    template <typename InputType, typename StateType, typename EventType>
    Chaos::FReal FPhysicsModel<InputType, StateType, EventType>::GetWorldTimeNoDilation() const {
        check(CachedComponent);
        if (const UWorld* World = CachedComponent->GetWorld()) {
            return World->GetRealTimeSeconds();
        }

        return -1.0;
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::GetNetworkConditions(FNetworkConditions& NetworkConditions) const {
        check(Delegate)
        Delegate->GetNetworkConditions(NetworkConditions);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ProduceInput(FInputPacketWrapper<InputType>& Packet) {
        ProduceInputDelegate.ExecuteIfBound(Packet.Body);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context,
                                                                            const InputType& Input,
                                                                            const FStateWrapper<StateType>& PrevState,
                                                                            FStateWrapper<StateType>& OutState) {
        SimOutput Output(OutState);
        SimulatePrePhysics(Dt, Context, Input, PrevState.Body, Output);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context,
                                                                             const InputType& Input,
                                                                             const FStateWrapper<StateType>& PrevState,
                                                                             FStateWrapper<StateType>& OutState) {
        SimOutput Output(OutState);
        SimulatePostPhysics(Dt, Context, Input, PrevState.Body, Output);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::DispatchEvents(const FStateWrapper<StateType>& State, const uint8 Events) {
        for (uint8 Event = 0; Event < 8; ++Event) {
            if ((Events & (0b1 << Event)) != 0) {
                DispatchEventDelegate.ExecuteIfBound(static_cast<EventType>(Event), State.Body, State.PhysicsState);
            }
        }
    }
}
