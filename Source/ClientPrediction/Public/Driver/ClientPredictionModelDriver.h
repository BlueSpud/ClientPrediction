#pragma once

#include "ClientPredictionNetSerialization.h"
#include "World/ClientPredictionTickCallback.h"
#include "Physics/ClientPredictionPhysicsContext.h"
#include "Driver/Input/ClientPredictionInput.h"
#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
    template <typename InputType, typename StateType>
    class IModelDriverDelegate {
    public:
        virtual ~IModelDriverDelegate() = default;

        virtual void GenerateInitialState(FStateWrapper<StateType>& State) = 0;
        virtual void Finalize(const StateType& State, Chaos::FReal Dt) = 0;

        virtual void EmitInputPackets(TArray<FInputPacketWrapper<InputType>>& Packets) = 0;
        virtual void EmitReliableAuthorityState(FStateWrapper<StateType> State) = 0;
        virtual void ProduceInput(InputType& Packet) = 0;
        virtual void ModifyInputPhysicsThread(InputType& Packet, const FStateWrapper<StateType>& State, Chaos::FReal Dt) = 0;

        virtual void SetTimeDilation(const Chaos::FReal TimeDilation) = 0;
        virtual void ForceSimulate(const uint32 NumTicks) = 0;
        virtual Chaos::FReal GetWorldTimeNoDilation() const = 0;
        virtual void GetNetworkConditions(FNetworkConditions& NetworkConditions) const = 0;

        virtual void SimulatePrePhysics(const Chaos::FReal Dt, FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                        FStateWrapper<StateType>& OutState) = 0;
        virtual void SimulatePostPhysics(const Chaos::FReal Dt, const FPhysicsContext& Context, const InputType& Input, const FStateWrapper<StateType>& PrevState,
                                         FStateWrapper<StateType>& OutState) = 0;

        virtual void DispatchEvents(const FStateWrapper<StateType>& State, const uint8 Events) = 0;
    };

    template <typename InputType, typename StateType>
    class IModelDriver : public ITickCallback {
    public:
        virtual ~IModelDriver() override = default;

        // Input packet / state receiving
        virtual void ReceiveInputPackets(const TArray<FInputPacketWrapper<InputType>>& Packets) {}
        virtual void ReceiveReliableAuthorityState(const FStateWrapper<StateType>& State) {}
    };
}
