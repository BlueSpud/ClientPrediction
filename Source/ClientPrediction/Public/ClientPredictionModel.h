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

	virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) = 0;
	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) = 0;

// Simulation ticking

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) = 0;
	virtual float GetTimescale() const = 0; 

	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha The percentage that time is between the current tick and the next tick.
	 * @param DeltaTime The time between the last finalized tick.
	 * @param Component The component that should be updated.
	 */
	virtual void Finalize(const Chaos::FReal Alpha, const Chaos::FReal DeltaTime, UPrimitiveComponent* Component) = 0;

// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) = 0;

public:

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitReliableAuthorityState;

};

/**********************************************************************************************************************/

enum EEmptyCueSet: uint8 {};

template <typename InputPacket, typename ModelState, typename CueSet = EEmptyCueSet>
class BaseClientPredictionModel : public IClientPredictionModel {
	using SimOutput = FSimulationOutput<ModelState, CueSet>;

public:

	BaseClientPredictionModel() = default;
	virtual ~BaseClientPredictionModel() override = default;

	virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override final;
	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) override final;

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override final;
	virtual float GetTimescale() const override final; 

	virtual void Finalize(Chaos::FReal Alpha, Chaos::FReal DeltaTime, UPrimitiveComponent* Component) override final;

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override final;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) override final;

public:

	DECLARE_DELEGATE_ThreeParams(FInputProductionDelgate, InputPacket&, const ModelState& State, Chaos::FReal Dt)
	FInputProductionDelgate InputDelegate;

	DECLARE_DELEGATE_TwoParams(FCueDelegate, const ModelState& CurrentState, CueSet Cue);
	FCueDelegate HandleCue;

	DECLARE_DELEGATE_TwoParams(FSimulationOutputDelegate, const ModelState&, const float)
	FSimulationOutputDelegate OnFinalized;

protected:

	virtual void Initialize(UPrimitiveComponent* Component) = 0;
	virtual void GenerateInitialState(ModelState& State) = 0;

	virtual void BeginTick(Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component);
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, SimOutput& Output, const InputPacket& Input) = 0;
	virtual void Rewind(const ModelState& State, UPrimitiveComponent* Component) = 0;

	/** Perform any internal logic for output of a state to be done on finalization */
	virtual void ApplyState(UPrimitiveComponent* Component, const ModelState& State);

private:

	TUniquePtr<IClientPredictionModelDriver<InputPacket, ModelState, CueSet>> Driver;
	bool bIsInitialized = false;

};

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveInputPackets(Proxy);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {
	if (Driver != nullptr) {
		Driver->ReceiveReliableAuthorityState(Proxy);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
	switch (Role) {
	case ROLE_Authority:
		Driver = MakeUnique<ClientPredictionAuthorityDriver<InputPacket, ModelState, CueSet>>(bShouldTakeInput);
		break;
	case ROLE_AutonomousProxy:
		Driver = MakeUnique<ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>>();
		break;
	case ROLE_SimulatedProxy:
		Driver = MakeUnique<ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>>();
		break;
	default:
		break;
	}

	Driver->EmitInputPackets = EmitInputPackets;
	Driver->EmitReliableAuthorityState = EmitReliableAuthorityState;
	Driver->InputDelegate = InputDelegate;
	Driver->BindToRepProxies(AutoProxyRep, SimProxyRep);
	Driver->GenerateInitialState = [&](ModelState& State) {
		GenerateInitialState(State);
	};

	Driver->Simulate = [&](Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, FSimulationOutput<ModelState, CueSet>& Output, const InputPacket& Input) {
		Simulate(Dt, Component, PrevState, Output, Input);
	};

	Driver->BeginTick = [&](Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component) {
		BeginTick(Dt, State, Component);
	};

	Driver->Rewind = [&](const ModelState& State, UPrimitiveComponent* Component) {
		Rewind(State, Component);
	};

	Driver->HandleCue = [&](const ModelState& State, CueSet Cue) {
		HandleCue.ExecuteIfBound(State, Cue);
	};

	if (bIsInitialized) {
		Driver->Initialize();
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::Initialize(UPrimitiveComponent* Component, ENetRole Role) {
	Initialize(Component);
	Driver->Initialize();

	bIsInitialized = true;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::BeginTick(Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	if (Driver == nullptr) { return; }
	Driver->Tick(Dt, Component);
}

template <typename InputPacket, typename ModelState, typename CueSet>
float BaseClientPredictionModel<InputPacket, ModelState, CueSet>::GetTimescale() const {
	if (Driver == nullptr) { return 1.0; }
	return Driver->GetTimescale();
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::ApplyState(UPrimitiveComponent* Component, const ModelState& State) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::Finalize(Chaos::FReal Alpha, Chaos::FReal DeltaTime, UPrimitiveComponent* Component) {
	if (Driver == nullptr) { return; }

	ModelState InterpolatedState = Driver->GenerateOutputGameDt(Alpha, DeltaTime);
	ApplyState(Component, InterpolatedState);

	OnFinalized.ExecuteIfBound(InterpolatedState, DeltaTime);
}
