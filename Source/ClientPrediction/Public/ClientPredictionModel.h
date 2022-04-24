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

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) = 0;

	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha the percentage that time is between the current tick and the next tick.
	 * @param Component The component that should be updated.
	 * @param Role The network role to finalize the output for */
	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component) = 0;

// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) = 0;
	
public:

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitAuthorityState;
	
};

template <typename InputPacket, typename ModelState>
class BaseClientPredictionModel : public IClientPredictionModel {
	
public:

	BaseClientPredictionModel() = default;
	virtual ~BaseClientPredictionModel() override = default;

	virtual void PreInitialize(ENetRole Role) override final;
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override final;

	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component) override final;
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override final;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override final;

public:
	
	DECLARE_DELEGATE_ThreeParams(FInputProductionDelgate, InputPacket&, const ModelState& State, Chaos::FReal Dt)
	FInputProductionDelgate InputDelegate;
	
	DECLARE_DELEGATE_OneParam(FSimulationOutputDelegate, const ModelState&)
	FSimulationOutputDelegate OnFinalized;
	
protected:

	virtual void BeginTick(Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component);
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) = 0;
	virtual void Rewind(const ModelState& State, UPrimitiveComponent* Component) = 0;

	/** Perform any internal logic for output of a state to be done on finalization */
	virtual void ApplyState(UPrimitiveComponent* Component, const ModelState& State);
	
private:

	TUniquePtr<IClientPredictionModelDriver<InputPacket, ModelState>> Driver;

};

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveInputPackets(Proxy);
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveAuthorityState(Proxy);
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::PreInitialize(ENetRole Role) {
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

	Driver->BeginTick = [&](Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component) {
		BeginTick(Dt, State, Component);
	};
	
	Driver->Rewind = [&](const ModelState& State, UPrimitiveComponent* Component) {
		Rewind(State, Component);
	};
	Driver->InputDelegate = InputDelegate;
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::BeginTick(Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	Driver->Tick(Dt, Component);	
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ApplyState(UPrimitiveComponent* Component, const ModelState& State) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component) {
	ModelState InterpolatedState = Driver->GenerateOutput(Alpha);
	ApplyState(Component, InterpolatedState);

	OnFinalized.ExecuteIfBound(InterpolatedState);
}
