#pragma once

#include "ClientPredictionNetSerialization.h"

#include "Driver/Drivers/ClientPredictionAuthorityModelDriver.h"
#include "Driver/Drivers/ClientPredictionAutoProxyModelDriver.h"
#include "Driver/Drivers/ClientPredictionSimProxyModelDriver.h"

#include "Driver/ClientPredictionModelDriver.h"

/**
 * The interface for the client prediction model. This exists so that the prediction component can hold a
 * reference to a templated model.
 */
class IClientPredictionModel {

public:

	IClientPredictionModel() = default;
	virtual ~IClientPredictionModel() = default;

	virtual void Initialize(UPrimitiveComponent* Component) = 0;
	virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) = 0;

// Simulation ticking

	virtual void Update(Chaos::FReal RealDt, UPrimitiveComponent* Component) = 0;

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
	TFunction<FNetworkConditions()> GetNetworkConditions;

};

/**********************************************************************************************************************/

enum EEmptyCueSet: uint8 {};

template <typename InputPacket, typename ModelState, typename CueSet = EEmptyCueSet>
class BaseClientPredictionModel : public IClientPredictionModel {
	using SimOutput = FSimulationOutput<ModelState, CueSet>;

public:

	BaseClientPredictionModel() = default;
	virtual ~BaseClientPredictionModel() override = default;

	virtual void Initialize(UPrimitiveComponent* Component) override;
	virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override final;

	virtual void Update(Chaos::FReal RealDt, UPrimitiveComponent* Component) override final;

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
	virtual void GenerateInitialState(ModelState& State) = 0;

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, SimOutput& Output, const InputPacket& Input) = 0;
	virtual void Rewind(const ModelState& State, UPrimitiveComponent* Component) = 0;

	/** Perform any internal logic for output of a state to be done on finalization */
	virtual void ApplyState(UPrimitiveComponent* Component, const ModelState& State);

private:
	static constexpr Chaos::FReal kMaxTimeDilationPercent = 0.25;

	TUniquePtr<IClientPredictionModelDriver<InputPacket, ModelState, CueSet>> Driver;
	bool bIsInitialized = false;

	Chaos::FReal AccumulatedTime = 0.0;
	Chaos::FReal AdjustmentTime = 0.0;

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
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::Initialize(UPrimitiveComponent* Component) {
	if (!bIsInitialized) {
		Driver->Initialize();
	}

	bIsInitialized = true;
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

	Driver->Rewind = [&](const ModelState& State, UPrimitiveComponent* Component) {
		Rewind(State, Component);
	};

	Driver->HandleCue = [&](const ModelState& State, CueSet Cue) {
		HandleCue.ExecuteIfBound(State, Cue);
	};

	Driver->GetNetworkConditions = GetNetworkConditions;
	Driver->AdjustTime = [&](const Chaos::FReal Adjustment) {
		AdjustmentTime = Adjustment;
	};

	if (bIsInitialized) {
		Driver->Initialize();
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionModel<InputPacket, ModelState, CueSet>::Update(Chaos::FReal RealDt, UPrimitiveComponent* Component) {
	if (Driver == nullptr) { return; }

	const Chaos::FReal MaxTimeDilation = RealDt * kMaxTimeDilationPercent;
	const Chaos::FReal Adjustment = FMath::Clamp(AdjustmentTime, -MaxTimeDilation, MaxTimeDilation);
	AdjustmentTime -= Adjustment;

	AccumulatedTime += RealDt + Adjustment;
	while (AccumulatedTime >= kFixedDt) {
		AccumulatedTime = FMath::Clamp(AccumulatedTime - kFixedDt, 0.0, AccumulatedTime);
		Driver->Tick(kFixedDt, AccumulatedTime, Component);
	}

	Finalize(AccumulatedTime / kFixedDt, RealDt, Component);
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
