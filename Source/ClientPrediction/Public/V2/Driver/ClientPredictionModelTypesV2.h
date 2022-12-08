#pragma once

namespace ClientPrediction {
	struct FPhysicsState {
		// TODO this probably needs some re-working since we probably want to sync the proxies / authority somehow
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

		void NetSerialize(FArchive& Ar);
	};

	inline void FPhysicsState::NetSerialize(FArchive& Ar) {
		Ar << TickNumber;
		Ar << InputPacketTickNumber;
		Ar << ObjectState;

		Ar << X;
		Ar << V;

		Ar << R;
		Ar << W;
	}

}