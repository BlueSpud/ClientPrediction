#pragma once

namespace ClientPrediction {

	extern CLIENTPREDICTION_API float ClientPredictionPositionTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionVelocityTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionRotationTolerance;
	extern CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance;

	struct CLIENTPREDICTION_API FPhysicsState {
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

		/**
		 * This uses a void pointer so that the drivers can avoid templates. The model has templates and handles
		 * creation of the bodies, so it can cast to them as needed. The shared pointers are smart enough to
		 * know how to destroy the bodies as well, so no manual casting / deleting is needed for that.
		 */
		TSharedPtr<void> Body = nullptr;

		void NetSerialize(FArchive& Ar);
		bool ShouldReconcile(const FPhysicsState& State) const;

		void FillState(const class Chaos::FRigidBodyHandle_Internal* Handle);
		void Reconcile(class Chaos::FRigidBodyHandle_Internal* Handle) const;
	};
}
