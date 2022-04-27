#pragma once

#include "ClientPredictionModelTypes.h"
#include "ClientPredictionNetSerialization.h"

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

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) = 0;

	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha the percentage that time is between the current tick and the next tick.
	 */
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) = 0;

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) = 0;
	
public:
	
	DECLARE_DELEGATE_ThreeParams(FInputProductionDelgate, InputPacket&, const ModelState& State, Chaos::FReal Dt)
	FInputProductionDelgate InputDelegate;
	
	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitAuthorityState;

	/** Simulation based functions */
	TFunction<void(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, FSimulationOutput<ModelState, CueSet>& Output, const InputPacket& Input)> Simulate;
	TFunction<void(const ModelState& State, UPrimitiveComponent* Component)> Rewind;
	TFunction<void(Chaos::FReal Dt, ModelState& State, UPrimitiveComponent* Component)> BeginTick;
	TFunction<void(const ModelState& State, CueSet Cue)> HandleCue;
};