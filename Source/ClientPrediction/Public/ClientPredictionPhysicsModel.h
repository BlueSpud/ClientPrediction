#pragma once

#include "ClientPredictionModel.h"
#include "ClientPredictionPhysics.h"

template <typename ModelState>
struct FPhysicsStateWrapper {
	
	ModelState State;
	FPhysicsState PhysicsState;

	void NetSerialize(FArchive& Ar);

	void Rewind(class UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle) const;

	bool operator ==(const FPhysicsStateWrapper<ModelState>& Other) const;
};

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::NetSerialize(FArchive& Ar) {
	State.NetSerialize(Ar);
	PhysicsState.NetSerialize(Ar);
}

template <typename ModelState>
void FPhysicsStateWrapper<ModelState>::Rewind(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle) const {
	PhysicsState.Rewind(Handle);
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

	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) override;

protected:
	
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, const State& PrevState, State& OutState, const InputPacket& Input) override final;
	virtual void PostSimulate(Chaos::FReal Dt, UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, State& OutState, const InputPacket& Input) override final;

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input);
	virtual void PostSimulate(Chaos::FReal Dt, UPrimitiveComponent* Component, ModelState& OutState, const InputPacket& Input);
};

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Initialize(UPrimitiveComponent* Component, ENetRole Role) {
	// Disable physics on simulated proxies since they will just mirror the server
	if (Role == ENetRole::ROLE_SimulatedProxy) {
		Component->SetSimulatePhysics(false);
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt,
UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, const State& PrevState, State& OutState, const InputPacket& Input) {
	Simulate(Dt, Component, Handle, PrevState.State, OutState.State, Input);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::PostSimulate(Chaos::FReal Dt,
UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, State& OutState, const InputPacket& Input) {
	FTransform WorldTransform = Handle->GetWorldTransform();
	
	OutState.PhysicsState.Location = WorldTransform.GetLocation();
	OutState.PhysicsState.Rotation = WorldTransform.GetRotation();
	OutState.PhysicsState.LinearVelocity = Handle->GetLinearVelocity();
	OutState.PhysicsState.AngularVelocity = Handle->GetAngularVelocity();

	Component->SetWorldTransform(WorldTransform);
	
	PostSimulate(Dt, Component, OutState.State, Input);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt,
	UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::PostSimulate(Chaos::FReal Dt,
	UPrimitiveComponent* Component, ModelState& OutState, const InputPacket& Input) {}