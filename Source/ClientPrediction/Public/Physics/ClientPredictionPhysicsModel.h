#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "Driver/ClientPredictionModelDriver.h"

#include "Driver/Drivers/ClientPredictionModelAuthDriver.h"
#include "Driver/Drivers/ClientPredictionModelAutoProxyDriver.h"
#include "Driver/Drivers/ClientPredictionModelSimProxyDriver.h"
#include "World/ClientPredictionWorldManager.h"
#include "Data/ClientPredictionModelId.h"

namespace ClientPrediction {
    // Delegate
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct IPhysicsModelDelegate {
        virtual ~IPhysicsModelDelegate() = default;

        virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
        virtual void GetNetworkConditions(FNetworkConditions& NetworkConditions) const = 0;
    };

    // Interface
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct FPhysicsModelBase {
        virtual ~FPhysicsModelBase() = default;

        virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) = 0;
        virtual void Cleanup() = 0;

        virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& ControlProxyRep) = 0;
        virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const = 0;
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

        void EndSimulation() {
            StateWrapper.bIsFinalState = true;
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
        virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& Output) = 0;

        /**
         * Simulates the model after physics has been run for this ticket.
         * WARNING: This is called on the physics thread, so any objects shared between the physics thread and the
         * game thread need to be properly synchronized.
         */
        virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const StateType& PrevState, SimOutput& Output) = 0;

        /** Performs any additional setup for the initial simulation state. */
        virtual void GenerateInitialState(StateType& State) const {};

        void SetModelId(const FClientPredictionModelId& NewModelId);

        // FPhysicsModelBase
        virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) override final;

        virtual ~FPhysicsModel() override = default;
        virtual void Cleanup() override final;

        virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& ControlProxyRep) override final;
        virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const override final;

        virtual void SetTimeDilation(const Chaos::FReal TimeDilation) override final;
        virtual void GetNetworkConditions(FNetworkConditions& NetworkConditions) const override final;

        // IModelDriverDelegate
        virtual void GenerateInitialState(FStateWrapper<StateType>& State) override final;
        virtual void Finalize(const StateType& State, Chaos::FReal Dt) override final;

        virtual void EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) override final;
        virtual void ProduceInput(InputType& Packet) override final;
        virtual void ModifyInputPhysicsThread(InputType& Packet, const FStateWrapper<StateType>& State, Chaos::FReal Dt) override final;

        virtual void SimulatePrePhysics(Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                        FStateWrapper<StateType>& OutState) override final;

        virtual void SimulatePostPhysics(Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                         FStateWrapper<StateType>& OutState) override final;

        virtual void DispatchEvents(const FStateWrapper<StateType>& State, uint8 Events, Chaos::FReal EstimatedWorldDelay) override final;

        virtual void ProcessExternalStimulus(StateType& State) override final;

        virtual void EndSimulation() override final;

    public:
        DECLARE_DELEGATE_OneParam(FPhysicsModelProduceInput, InputType&)
        FPhysicsModelProduceInput ProduceInputDelegate;

        /**
         * This delegate will be executed on the PHYSICS THREAD and can be used to modify input packets using the current state of the simulation.
         * This will be called ONLY ONCE per input packet, and any changes made an an auto proxy will also be sent to the authority.
         */
        DECLARE_DELEGATE_FourParams(FPhysicsModelModifyInputPhysicsThread, InputType&, const StateType&, const FPhysicsState&, Chaos::FReal)
        FPhysicsModelModifyInputPhysicsThread ModifyInputPhysicsThreadDelegate;

        DECLARE_DELEGATE_TwoParams(FPhysicsModelFinalize, const StateType&, Chaos::FReal)
        FPhysicsModelFinalize FinalizeDelegate;

        /**
         * The last Chaos::FReal parameter contains the estimated delay since the event was actually executed. For sim proxies and auto proxies, this is always 0.0
         * since there is no delay. However, for the authority has to wait for inputs from the client and then buffers them, there is latency between the auto proxy
         * seeing an event and the authority seeing it. This value can be used to rollback the world for things like hit detection. However, the value is calculated
         * from a tick index provided by the auto proxy so it could potentially be used to cheat. For that reason, care needs to be taken when using it to ensure that
         * play remains fair.
         */
        DECLARE_DELEGATE_FourParams(FPhysicsModelDispatchEvent, EventType, const StateType&, const FPhysicsState&, const Chaos::FReal)
        FPhysicsModelDispatchEvent DispatchEventDelegate;

        /**
         * This delegate will be executed on the PHYSICS THREAD and can be used to modify states post tick.
         * This is a good place to apply the result of things that exist external to this simulation. For instance, this would be a good place to subtract health
         * as a result of being hit by a projectile.
         *
         * This is not called for simulated proxies.
         */
        DECLARE_DELEGATE_OneParam(FPhysicsModelProcessExternalStimulus, StateType&)
        FPhysicsModelProcessExternalStimulus ProcessExternalStimulusDelegate;

        DECLARE_DELEGATE(FPhysicsModelEndSimulation)
        FPhysicsModelEndSimulation EndSimulationDelegate;

    private:
        class UPrimitiveComponent* CachedComponent = nullptr;
        struct FWorldManager* CachedWorldManager = nullptr;
        TUniquePtr<IModelDriver<InputType, StateType>> ModelDriver = nullptr;
        IPhysicsModelDelegate* Delegate = nullptr;

        FClientPredictionModelId ModelId{};
    };

    // Implementation
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SetModelId(const FClientPredictionModelId& NewModelId) {
        ModelId = NewModelId;
    }

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
        // Sometimes the world manager might get cleaned up first, so we want to check if it actually exists
        const UWorld* World = CachedComponent->GetWorld();
        if (World == nullptr) { return; }

        CachedWorldManager = FWorldManager::ManagerForWorld(World);
        if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
            ModelDriver->Unregister(CachedWorldManager, ModelId);
        }

        CachedComponent = nullptr;
        CachedWorldManager = nullptr;
        ModelDriver = nullptr;
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::SetNetRole(ENetRole Role, bool bShouldTakeInput, FRepProxy& ControlProxyRep) {
        check(CachedWorldManager)
        check(CachedComponent)
        check(Delegate)

        if (ModelDriver != nullptr) {
            ModelDriver->Unregister(CachedWorldManager, ModelId);
        }

        int32 RewindBufferSize = CachedWorldManager->GetRewindBufferSize();
        switch (Role) {
        case ROLE_Authority:
            ModelDriver = MakeUnique<FModelAuthDriver<InputType, StateType>>(CachedComponent, this, ControlProxyRep, RewindBufferSize, bShouldTakeInput);
            break;
        case ROLE_AutonomousProxy: {
            ModelDriver = MakeUnique<FModelAutoProxyDriver<InputType, StateType>>(CachedComponent, this, ControlProxyRep, RewindBufferSize);
        }
        break;
        case ROLE_SimulatedProxy:
            ModelDriver = MakeUnique<FModelSimProxyDriver<InputType, StateType>>(CachedComponent, this);
            break;
        default:
            break;
        }

        ModelDriver->Register(CachedWorldManager, ModelId);
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
        else {
            State.PhysicsState.ObjectState = Chaos::EObjectStateType::Uninitialized;

            State.PhysicsState.X = CachedComponent->GetComponentLocation();
            State.PhysicsState.V = CachedComponent->GetComponentVelocity();
            State.PhysicsState.R = CachedComponent->GetComponentRotation().Quaternion();
            State.PhysicsState.W = CachedComponent->GetPhysicsAngularVelocityInDegrees();
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
    void FPhysicsModel<InputType, StateType, EventType>::SetTimeDilation(const Chaos::FReal TimeDilation) {
        check(CachedWorldManager)
        CachedWorldManager->SetTimeDilation(TimeDilation);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::GetNetworkConditions(FNetworkConditions& NetworkConditions) const {
        check(Delegate)
        Delegate->GetNetworkConditions(NetworkConditions);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ProduceInput(InputType& Packet) {
        ProduceInputDelegate.ExecuteIfBound(Packet);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ModifyInputPhysicsThread(InputType& Packet, const FStateWrapper<StateType>& State, Chaos::FReal Dt) {
        ModifyInputPhysicsThreadDelegate.ExecuteIfBound(Packet, State.Body, State.PhysicsState, Dt);
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
    void FPhysicsModel<InputType, StateType,
                       EventType>::DispatchEvents(const FStateWrapper<StateType>& State, const uint8 Events, const Chaos::FReal EstimatedWorldDelay) {
        for (uint8 Event = 0; Event < 8; ++Event) {
            if ((Events & (0b1 << Event)) != 0) {
                DispatchEventDelegate.ExecuteIfBound(static_cast<EventType>(Event), State.Body, State.PhysicsState, EstimatedWorldDelay);
            }
        }
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::ProcessExternalStimulus(StateType& State) {
        ProcessExternalStimulusDelegate.ExecuteIfBound(State);
    }

    template <typename InputType, typename StateType, typename EventType>
    void FPhysicsModel<InputType, StateType, EventType>::EndSimulation() {
        EndSimulationDelegate.ExecuteIfBound();
    }
}
