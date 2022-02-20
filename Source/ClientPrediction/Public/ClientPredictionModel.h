#pragma once

#include "ClientPredictionNetSerialization.h"
#include "Input.h"

/**
 * The interface for the client prediction model. This exists so that the prediction component can hold a
 * reference to a templated model.
 */
class IClientPredictionModel {
	
public:

	IClientPredictionModel() = default;
	virtual ~IClientPredictionModel() = default;

// Net serialization
	
	virtual void NetSerializeInputPacket(FArchive& Ar) = 0;		
	virtual void NetSerializeStatePacket(FArchive& Ar) = 0;		

// Simulation ticking

	virtual void PreTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) = 0;
	virtual void PostTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) = 0;

// Input packet / state receiving

	virtual void ReceiveInputPacket(FNetSerializationProxy& Proxy);
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy);
	
public:
	
	/** Simulate for the given number of tickets */
	TFunction<void(uint32)> Simulate;

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy)> EmitInputPacket;
	TFunction<void(FNetSerializationProxy)> EmitAuthorityState;
	
};

template <typename InputPacket, typename SimulationState>
class BaseClientPredictionModel : public IClientPredictionModel {

	// TODO wrap InputPacket and SimulationState so subclasses don't have access to the frame number
	
public:
	virtual void NetSerializeInputPacket(FArchive& Ar) override final;
	virtual void NetSerializeStatePacket(FArchive& Ar) override final;

	virtual void PreTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) override final;
	virtual void PostTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) override final;
	
	virtual void ReceiveInputPacket(FNetSerializationProxy& Proxy) override final;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override final;

public:
	
	DECLARE_DELEGATE_OneParam(FInputProductionDelgate, InputPacket&)
	FInputProductionDelgate InputDelegate;
	
protected:

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const SimulationState& PrevState, SimulationState& OutState, const InputPacket& Input) = 0;
	
	virtual void Rewind(const SimulationState& State, UPrimitiveComponent* Component) const;
	
private:
	
	/**
	 * If this object belongs to a client, the last acknowledged frame from the server.
	 * At this frame the client was identical to the server. 
	 */
	uint32 AckedServerFrame = FPhysicsState::kInvalidFrame;
	
	uint32 NextLocalFrame = 0;

	/** Client index for the next input packet number */
	uint32 NextInputPacket = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacket = FPhysicsState::kInvalidFrame;
	
	/** On the client this is all of the frames that have not been reconciled with the server. */
	TQueue<SimulationState> ClientHistory;

	/**
	 * The last state that was received from the authority.
	 * We use atomic here because the state will be written to from whatever thread handles receiving the state
	 * from the authority and be read from the physics thread.
	 */
	std::atomic<SimulationState> LastAuthorityState;

	FInputBuffer<InputPacket> InputBuffer;

};