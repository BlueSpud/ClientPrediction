#pragma once

#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionSimProxyDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;

public:

	ClientPredictionSimProxyDriver() = default;

	// Simulation ticking

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override {};
	virtual ModelState GenerateOutputGameDt(Chaos::FReal Alpha, Chaos::FReal GameDt) override;

	// This should never be called, since GenerateOutputGame
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override {
		check(false);
		return {};
	};

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) override;

	virtual void BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override;

private:

	void DispatchCues(const WrappedState& State);

private:
	/** The number of seconds of interpolation data that is desired to be stored in the buffer. */
	static constexpr float kDesiredInterpolationBufferTime = kDesiredInterpolationBufferMs / 1000.0;
	static constexpr float kDesiredInterpolationTooMuchTime = kDesiredInterpolationBufferTime * 1.5;
	static constexpr float kDesiredInterpolationTooLittleTime = kDesiredInterpolationBufferTime * 0.75;

	/** If for some reason the buffer is far in the future, we will fast forward. */
	static constexpr float kFastForwardTime = kDesiredInterpolationBufferMs * 3.0;

	/** This is how much time is sped up or slowed down to compensate for a buffer being to large or too small */
	static constexpr float kTimeDilation = 0.2;

	uint32 CurrentFrame = kInvalidFrame;
	float AccumulatedGameTime = 0.0;

	/**
	 *The frame number of the last state that was popped off the interpolation buffer.
	 * Anything before this is considered "in the past"
	 */
	uint32 LastPoppedFrame = kInvalidFrame;

	TArray<WrappedState> States;
};

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::GenerateOutputGameDt(Chaos::FReal ModelAlpha, Chaos::FReal GameDt) {
	if (States.IsEmpty()) { return {}; }

	if (CurrentFrame == kInvalidFrame) {
		const float TimeInBuffer = static_cast<float>(States.Last().FrameNumber - States[0].FrameNumber) * kFixedDt;
		if (TimeInBuffer < kDesiredInterpolationBufferTime) { return States[0].State; }

		CurrentFrame = States[0].FrameNumber;
		DispatchCues(States[0]);

		return States[0].State;
	}

	// Adjust the playback speed of the buffer based on if there is too much or too little
	float TimeLeftInBuffer = static_cast<float>(States.Last().FrameNumber - CurrentFrame) * kFixedDt - AccumulatedGameTime;
	if (TimeLeftInBuffer < 0.0) { TimeLeftInBuffer = 0.0; }

	float Timescale = 1.0;
	if (TimeLeftInBuffer <= kDesiredInterpolationTooLittleTime) { Timescale = 1.0 - kTimeDilation; }
	if (TimeLeftInBuffer >= kDesiredInterpolationTooMuchTime) { Timescale = 1.0 + kTimeDilation; }
	AccumulatedGameTime += Timescale * GameDt;

	uint32 PreviousFrame = CurrentFrame;
	CurrentFrame += static_cast<uint32>(AccumulatedGameTime / kFixedDt);
	AccumulatedGameTime = FMath::Fmod(AccumulatedGameTime, kFixedDt);

	// We don't want to go into the future
	if (CurrentFrame >= States.Last().FrameNumber) {
		WrappedState Last = States.Last();
		CurrentFrame = Last.FrameNumber;

		States.Empty();
		States.Add(Last);
		return Last.State;
	}

	if (TimeLeftInBuffer >= kFastForwardTime) {
		UE_LOG(LogTemp, Warning, TEXT("Client is very far behind for a simulated proxy, fast forwarding..."));
		CurrentFrame += static_cast<uint32>((kFastForwardTime - kDesiredInterpolationBufferTime));
		AccumulatedGameTime = 0;
	}

	if (PreviousFrame != CurrentFrame) {
		for (const WrappedState& State : States) {
			if (State.FrameNumber > PreviousFrame && State.FrameNumber <= CurrentFrame) {
				DispatchCues(State);
			}
		}
	}

	// Figure out where in the buffer we are
	int32 BeginFrameIndex = 0;
	for (; BeginFrameIndex < States.Num() - 1; BeginFrameIndex++) {
		if (States[BeginFrameIndex + 1].FrameNumber > CurrentFrame) {
			break;
		}
	}

	// If we made it here, we should be interpolating between two frames
	check(BeginFrameIndex != States.Num() - 1);

	// Remove any states that are not longer needed
	for (int32 i = 0; i < BeginFrameIndex; i++) {
		LastPoppedFrame = States[0].FrameNumber;
		States.RemoveAt(0);
	}

	const WrappedState& StartState = States[0];
	const WrappedState& EndState = States[1];

	const float TimeDelta = static_cast<float>(EndState.FrameNumber - StartState.FrameNumber);
	const float Progress = static_cast<float>(CurrentFrame - StartState.FrameNumber) + (AccumulatedGameTime / kFixedDt);
	const float Alpha = Progress / TimeDelta;

	ModelState FinalState = StartState.State;
	FinalState.Interpolate(Alpha, EndState.State);

	return FinalState;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	// No-op, sim proxy should never get input
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) {
	WrappedState State;

	Proxy.NetSerializeFunc = [&](FArchive& Ar) {
		State.NetSerialize(Ar);
	};

	Proxy.Deserialize();

	// Once CurrentFrame is set, it is assumed that all cues have been handled for that frame. So if this state is before that,
	// it needs to have the cues dispatched.
	if (CurrentFrame != kInvalidFrame && State.FrameNumber <= CurrentFrame) {
		DispatchCues(State);
	}

	// This is sent from an RPC (since it is reliable) and therefore might be out of order in relation to what comes out of the replication proxy (which is a property).
	// Just in case, we sort all of the states.
	States.Add(State);
	States.Sort([](const WrappedState& A, const WrappedState& B) {
		return A.FrameNumber < B.FrameNumber;
	});
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
	SimProxyRep.SerializeFunc = [&](FArchive& Ar) {
		WrappedState State;
		State.NetSerialize(Ar);

		// Don't add any states that are "in the past"
		if (LastPoppedFrame != kInvalidFrame && State.FrameNumber <= LastPoppedFrame) {
			UE_LOG(LogTemp, Log, TEXT("Ignoring state that was in the past"));
			return;
		}

		// Once CurrentFrame is set, it is assumed that all cues have been handled for that frame. So if this state is before that,
		// it needs to have the cues dispatched.
		if (CurrentFrame != kInvalidFrame && State.FrameNumber <= CurrentFrame) {
			DispatchCues(State);
		}

		States.Add(State);
	};
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::DispatchCues(const WrappedState& State) {
	if (!State.Cues.IsEmpty()) {
		for (int Cue = 0; Cue < State.Cues.Num(); Cue++) {
			HandleCue(State.State, static_cast<CueSet>(State.Cues[Cue]));
		}
	}
}
