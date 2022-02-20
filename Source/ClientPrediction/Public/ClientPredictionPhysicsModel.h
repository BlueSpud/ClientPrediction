﻿#pragma once

#include "ClientPredictionModel.h"
#include "ClientPredictionPhysics.h"

template <typename ModelState>
struct FPhysicsStateWrapper {
	
	ModelState State;
	FPhysicsState PhysicsState;

	void NetSerialize(FArchive& Ar);

	void Rewind(class UPrimitiveComponent* Component) const;

	bool operator ==(const FPhysicsStateWrapper<ModelState>& Other) const;
};

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::NetSerialize(FArchive& Ar) {
	State.NetSerialize(Ar);
	PhysicsState.NetSerialize(Ar);
}

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::Rewind(UPrimitiveComponent* Component) const {
	PhysicsState.Rewind(Component);
	State.Rewind(Component);
}

template <typename ModelState>
bool FPhysicsStateWrapper<ModelState>::operator==(const FPhysicsStateWrapper<ModelState>& Other) const {
	return State == Other.State
		&& PhysicsState == Other.PhysicsState;
}

/**********************************************************************************************************************/

struct CLIENTPREDICTION_API FEmptyState {
	void NetSerialize(FArchive& Ar) {}
	void Rewind(class UPrimitiveComponent* Component) const {}
	bool operator ==(const FEmptyState& Other) const { return true; }
};

template <typename InputPacket, typename ModelState>
class BaseClientPredictionPhysicsModel : public BaseClientPredictionModel<InputPacket, FPhysicsStateWrapper<ModelState>> {
	
using State = FPhysicsStateWrapper<ModelState>;

public:

	BaseClientPredictionPhysicsModel() = default;
	virtual ~BaseClientPredictionPhysicsModel() override = default;
	
protected:
	
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const State& PrevState, State& OutState, const InputPacket& Input) override final;
	virtual void PostSimulate(Chaos::FReal Dt, UPrimitiveComponent* Component, State& OutState, const InputPacket& Input) override final;

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input);
	virtual void PostSimulate(Chaos::FReal Dt, UPrimitiveComponent* Component, ModelState& OutState, const InputPacket& Input);
};

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt,
UPrimitiveComponent* Component, const State& PrevState, State& OutState, const InputPacket& Input) {
	Simulate(Dt, Component, PrevState.State, OutState.State, Input);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::PostSimulate(Chaos::FReal Dt,
UPrimitiveComponent* Component, State& OutState, const InputPacket& Input) {
	FBodyInstance* Body = Component->GetBodyInstance();
	Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
	if (!Handle) {
		return;
	}
	
	check(Handle);
	check(Handle->CanTreatAsKinematic());
		
	OutState.PhysicsState.Location = Handle->X();
	OutState.PhysicsState.Rotation = Handle->R();
	OutState.PhysicsState.LinearVelocity = Handle->V();
	OutState.PhysicsState.AngularVelocity = Handle->W();

	PostSimulate(Dt, Component, OutState.State, Input);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt,
	UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::PostSimulate(Chaos::FReal Dt,
	UPrimitiveComponent* Component, ModelState& OutState, const InputPacket& Input) {}

using ClientPredictionPhysicsModel = BaseClientPredictionPhysicsModel<FInputPacket, FEmptyState>;
