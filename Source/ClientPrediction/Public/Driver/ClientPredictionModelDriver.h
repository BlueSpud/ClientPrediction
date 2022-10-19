#pragma once

#include "ClientPredictionModelTypes.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionRepProxy.h"

/**
 * The interface for the client prediction model driver. This has different implementations based on the net role
 * of the owner of a model.
 */
template <typename InputPacket, typename ModelState, typename CueSet>
class IClientPredictionModelDriver {

public:

	IClientPredictionModelDriver() = default;
	virtual ~IClientPredictionModelDriver() = default;

	// Simulation ticking

	/** To be called after all of the delegate functions are set */
	virtual void Initialize() {};
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) = 0;

	/**
	 * To be called after ticks have been performed and finalizes the output from the model. By default,
	 * this will generate an alpha value and call GenerateOutput.
	 * @param Alpha The percentage that time is between the current tick and the next tick.
	 * @param GameDt The GAME time that has elapsed since the last call to GenerateOutputGameDt.
	 */
	virtual ModelState GenerateOutputGameDt(Chaos::FReal Alpha, Chaos::FReal GameDt);
	
	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha The percentage that time is between the current tick and the next tick.
	 */
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) = 0;

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {};
	virtual void BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) = 0;

	// Time dilation
	virtual float GetTimescale() const { return 1.0; }

public:

	DECLARE_DELEGATE_ThreeParams(FInputProductionDelgate, InputPacket&, const ModelState& State, Chaos::FReal Dt)
	FInputProductionDelgate InputDelegate;

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitReliableAuthorityState;

	/** Simulation based functions */
	TFunction<void(ModelState& State)> GenerateInitialState;
	TFunction<void(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, FSimulationOutput<ModelState, CueSet>& Output, const InputPacket& Input)> Simulate;
	TFunction<void(const ModelState& State, UPrimitiveComponent* Component)> Rewind;
	TFunction<void(const ModelState& State, CueSet Cue)> HandleCue;
};

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState IClientPredictionModelDriver<InputPacket, ModelState, CueSet>::GenerateOutputGameDt(Chaos::FReal Alpha, Chaos::FReal GameDt) {
	return GenerateOutput(Alpha);
}
