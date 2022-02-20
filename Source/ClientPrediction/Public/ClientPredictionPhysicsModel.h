#pragma once

#include "ClientPredictionModel.h"
#include "ClientPredictionPhysics.h"

template <typename ModelState>
struct FPhysicsStateWrapper {

	uint32 FrameNumber = kInvalidFrame;
	uint32 InputPacketNumber = kInvalidFrame;
	
	ModelState State;
	FPhysicsState PhysicsState;

	void NetSerialize(FArchive& Ar) {
		Ar << FrameNumber;
		Ar << InputPacketNumber;
		
		State.NetSerialize(Ar);
		PhysicsState.NetSerialize(Ar);
	}

	void Rewind(class UPrimitiveComponent* Component) const {
		PhysicsState.Rewind(Component);
		State.Rewind(Component);
	}

	bool operator ==(const FPhysicsStateWrapper<ModelState>& Other) const {
		return InputPacketNumber == Other.InputPacketNumber
			&& State == Other.State
			&& PhysicsState == Other.PhysicsState;
	}
};

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
	
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const State& PrevState, State& OutState, const InputPacket& Input) override;

	virtual void PostSimulate(Chaos::FReal Dt, UPrimitiveComponent* Component, State& OutState, const InputPacket& Input) override;
	
};

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt,
UPrimitiveComponent* Component, const State& PrevState, State& OutState, const InputPacket& Input) {
	if (Input.bIsApplyingForce) {
		FBodyInstance* Body = Component->GetBodyInstance();
		Chaos::FRigidBodyHandle_Internal* Handle = Body->GetPhysicsActorHandle()->GetPhysicsThreadAPI();
		Handle->AddForce(Chaos::FVec3(0.0, 0.0, 1000000.0));
	}
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
}

using ClientPredictionPhysicsModel = BaseClientPredictionPhysicsModel<FInputPacket, FEmptyState>;