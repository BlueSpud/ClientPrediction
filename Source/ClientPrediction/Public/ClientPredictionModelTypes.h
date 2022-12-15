#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {

	extern CLIENTPREDICTION_API float ClientPredictionPositionTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionVelocityTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionRotationTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance;

	template <typename StateType>
	struct FPhysicsState {
		// TODO this maybe needs some re-working since we probably want to sync the proxies / authority somehow
		/** The tick number that the state was generated on. So if receiving from the authority, this is the index according to the authority. */
		int32 TickNumber = INDEX_NONE;

		/** This is the tick index the input was sampled ON THE AUTO PROXY. */
		int32 InputPacketTickNumber = INDEX_NONE;

		/** These mirror the Chaos properties for a particle */
		Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

		Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
		Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

		Chaos::FRotation3 R = Chaos::FRotation3::Identity;
		Chaos::FVec3 W = Chaos::FVec3::ZeroVector;

		StateType Body{};

		// These are not sent over the network, they're used for local interpolation only
		float StartTime = 0.0;
		float EndTime = 0.0;

		void NetSerialize(FArchive& Ar);
		bool ShouldReconcile(const FPhysicsState& State) const;

		void FillState(const class Chaos::FRigidBodyHandle_Internal* Handle);
		void Reconcile(class Chaos::FRigidBodyHandle_Internal* Handle) const;
	};

	template <typename StateType>
	void FPhysicsState<StateType>::NetSerialize(FArchive& Ar) {
		Ar << TickNumber;
		Ar << InputPacketTickNumber;
		Ar << ObjectState;

		// Serialize manually to make sure that they are serialized as doubles
		Ar << X.X;
		Ar << X.Y;
		Ar << X.Z;

		Ar << V.X;
		Ar << V.Y;
		Ar << V.Z;

		Ar << R.X;
		Ar << R.Y;
		Ar << R.Z;
		Ar << R.W;

		Ar << W.X;
		Ar << W.Y;
		Ar << W.Z;

		Body.NetSerialize(Ar);
	}

	template <typename StateType>
	bool FPhysicsState<StateType>::ShouldReconcile(const FPhysicsState& State) const {
		if (State.ObjectState != ObjectState) { return true; }
		if ((State.X - X).Size() > ClientPredictionPositionTolerance) { return true; }
		if ((State.V - V).Size() > ClientPredictionVelocityTolerance) { return true; }
		if ((State.R - R).Size() > ClientPredictionRotationTolerance) { return true; }
		if ((State.W - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

		return Body.ShouldReconcile(State.Body);
	}

	template <typename StateType>
	void FPhysicsState<StateType>::FillState(const Chaos::FRigidBodyHandle_Internal* Handle) {
		X = Handle->X();
		V = Handle->V();
		R = Handle->R();
		W = Handle->W();
		ObjectState = Handle->ObjectState();
	}

	template <typename StateType>
	void FPhysicsState<StateType>::Reconcile(Chaos::FRigidBodyHandle_Internal* Handle) const {
		Handle->SetX(X);
        Handle->SetV(V);
        Handle->SetR(R);
        Handle->SetW(W);
        Handle->SetObjectState(ObjectState);
	}
}
