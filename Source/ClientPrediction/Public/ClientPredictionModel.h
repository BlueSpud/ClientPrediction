#pragma once

#include "ClientPredictionNetSerialization.h"
#include "Driver/ClientPredictionAuthorityModelDriver.h"
#include "Driver/ClientPredictionAutoProxyModelDriver.h"

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionSimProxyModelDriver.h"

/**
 * The interface for the client prediction model. This exists so that the prediction component can hold a
 * reference to a templated model.
 */
class IClientPredictionModel {
	
public:

	IClientPredictionModel() = default;
	virtual ~IClientPredictionModel() = default;

	virtual void PreInitialize(ENetRole Role) = 0;
	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) = 0;

// Simulation ticking

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) = 0;

	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha the percentage that time is between the current tick and the next tick.
	 * @param Component The component that should be updated.
	 * @param Role The network role to finalize the output for */
	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) = 0;

// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) = 0;
	
public:

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitAuthorityState;
	
};

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
class BaseClientPredictionModel : public IClientPredictionModel {
	
public:

	BaseClientPredictionModel() = default;
	virtual ~BaseClientPredictionModel() override = default;

	virtual void PreInitialize(ENetRole Role) override final;

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) override final;

	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) override final;
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override final;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override final;

public:
	
	DECLARE_DELEGATE_OneParam(FInputProductionDelgate, InputPacket&)
	FInputProductionDelgate InputDelegate;
	
	DECLARE_DELEGATE_OneParam(FSimulationOutputDelegate, const ModelStateOutput&)
	FSimulationOutputDelegate OnFinalized;
	
protected:

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) = 0;
	virtual void Rewind(const ModelState& State, UPrimitiveComponent* Component) = 0;

	/** Perform any internal logic for output of a state to be done on finalization */
	virtual void ApplyState(UPrimitiveComponent* Component, const ModelState& State);

	/**
	 * Calls the OnFinalized delegate with the state. This methods is purely virtual because if some mapping
	 * needs to be performed, there isn't a base class implementation that will compile.
	 * @param State The state to emit.
	 */ 
	virtual void CallOnFinalized(const ModelState& State) = 0;
	
private:

	TUniquePtr<IClientPredictionModelDriver<InputPacket, ModelState>> Driver;

};

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveInputPackets(Proxy);
	}
}

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveAuthorityState(Proxy);
	}
}

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::PreInitialize(ENetRole Role) {
	switch (Role) {
	case ROLE_Authority:
		Driver = MakeUnique<ClientPredictionAuthorityDriver<InputPacket, ModelState>>();
		break;
	case ROLE_AutonomousProxy:
		Driver = MakeUnique<ClientPredictionAutoProxyDriver<InputPacket, ModelState>>();
		break;
	case ROLE_SimulatedProxy:
		Driver = MakeUnique<ClientPredictionSimProxyDriver<InputPacket, ModelState>>();
		break;
	default:
		break;
	}

	Driver->EmitInputPackets = EmitInputPackets;
	Driver->EmitAuthorityState = EmitAuthorityState;
	Driver->Simulate = [&](Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) {
		Simulate(Dt, Component, PrevState, OutState, Input);
	};
	
	Driver->Rewind = [&](const ModelState& State, UPrimitiveComponent* Component) {
		Rewind(State, Component);
	};

	Driver->InputDelegate = InputDelegate;
}

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) {
	Driver->Tick(Dt, Component);	
}

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::ApplyState(UPrimitiveComponent* Component, const ModelState& State) {}

template <typename InputPacket, typename ModelState, typename ModelStateOutput>
void BaseClientPredictionModel<InputPacket, ModelState, ModelStateOutput>::Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) {
	ModelState InterpolatedState = Driver->GenerateOutput(Alpha);
	ApplyState(Component, InterpolatedState);

	CallOnFinalized(InterpolatedState);
}
