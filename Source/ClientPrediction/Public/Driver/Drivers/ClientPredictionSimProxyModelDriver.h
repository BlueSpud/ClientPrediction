#pragma once

#include "../ClientPredictionModelDriver.h"
#include "../ClientPredictionModelTypes.h"

#include "../../ClientPredictionNetSerialization.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionSimProxyDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;

public:

	ClientPredictionSimProxyDriver() = default;
	virtual void Initialize() override;

	// Simulation ticking

	virtual void Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component) override {};
	virtual ModelState GenerateOutputGameDt(Chaos::FReal Alpha, Chaos::FReal GameDt) override;

	// This should never be called, since GenerateOutputGameDt is overriden
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override {
		check(false);
		return {};
	};

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) override;

	virtual void BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override;

private:

	void ProcessAuthorityState(const WrappedState& State);
	void DispatchCues(const WrappedState& State);

private:
	/** The number of seconds of interpolation data that is desired to be stored in the buffer. */
	static constexpr float kDesiredInterpolationBufferTime = kDesiredInterpolationBufferMs / 1000.0;
	static constexpr float kDesiredInterpolationTooMuchTime = kDesiredInterpolationBufferTime + kFixedDt * 2.0;
	static constexpr float kDesiredInterpolationTooLittleTime = kDesiredInterpolationBufferTime - kFixedDt * 2.0;
	static constexpr Chaos::FReal kFastForwardTime = kDesiredInterpolationBufferTime * 2.0;
	static constexpr Chaos::FReal kMaxTimeDilationPercent = 0.05;

	uint32 CurrentFrame = kInvalidFrame;
	Chaos::FReal AccumulatedGameTime = 0.0;

	/**
	 *The frame number of the last state that was popped off the interpolation buffer.
	 * Anything before this is considered "in the past"
	 */
	uint32 LastPoppedFrame = kInvalidFrame;

	TArray<WrappedState> States;
	ModelState InitialState;
};

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::Initialize() {
	GenerateInitialState(InitialState);
}

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::GenerateOutputGameDt(Chaos::FReal ModelAlpha, Chaos::FReal GameDt) {
	if (States.IsEmpty()) { return InitialState; }

	if (CurrentFrame == kInvalidFrame) {
		const Chaos::FReal TimeInBuffer = static_cast<Chaos::FReal>(States.Last().FrameNumber - States[0].FrameNumber) * kFixedDt;
		if (TimeInBuffer < kDesiredInterpolationBufferTime) { return States[0].State; }

		CurrentFrame = States[0].FrameNumber;
		DispatchCues(States[0]);

		return States[0].State;
	}

	// Adjust the playback speed of the buffer based on if there is too much or too little
	Chaos::FReal TimeLeftInBuffer = static_cast<Chaos::FReal>(States.Last().FrameNumber - CurrentFrame) * kFixedDt - (AccumulatedGameTime + GameDt);
	if (TimeLeftInBuffer < 0.0) { TimeLeftInBuffer = 0.0; }

	// Adjust the speed that time is ticking to try to keep the buffer at the target
	Chaos::FReal AdjustmentTime = 0.0;
	if (TimeLeftInBuffer <= kDesiredInterpolationTooLittleTime) { AdjustmentTime = TimeLeftInBuffer - kDesiredInterpolationTooLittleTime; }
	if (TimeLeftInBuffer >= kDesiredInterpolationTooMuchTime) { AdjustmentTime = TimeLeftInBuffer - kDesiredInterpolationTooMuchTime; }

	const Chaos::FReal MaxTimeDilation = GameDt * kMaxTimeDilationPercent;
	AdjustmentTime = FMath::Clamp(AdjustmentTime, -MaxTimeDilation, MaxTimeDilation);

	// If there is far too much time in the buffer, just fast forward
	if (TimeLeftInBuffer > kFastForwardTime) {
		AdjustmentTime = (TimeLeftInBuffer - kFastForwardTime);
	}

	uint32 PreviousFrame = CurrentFrame;
	AccumulatedGameTime += GameDt + AdjustmentTime;

	while (AccumulatedGameTime > kFixedDt) {
		AccumulatedGameTime -= kFixedDt;
		++CurrentFrame;
	}

	if (PreviousFrame != CurrentFrame) {
		for (const WrappedState& State : States) {
			if (State.FrameNumber > PreviousFrame && State.FrameNumber <= CurrentFrame) {
				DispatchCues(State);
			}
		}
	}

	// We don't want to go into the future
	if (CurrentFrame >= States.Last().FrameNumber) {
		WrappedState Last = States.Last();
		CurrentFrame = Last.FrameNumber;

		States.Empty();
		States.Add(Last);
		return Last.State;
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

	const Chaos::FReal TimeDelta = static_cast<Chaos::FReal>(EndState.FrameNumber - StartState.FrameNumber) * kFixedDt;
	const Chaos::FReal Progress = static_cast<Chaos::FReal>(CurrentFrame - StartState.FrameNumber) * kFixedDt + AccumulatedGameTime;
	const Chaos::FReal Alpha = Progress / TimeDelta;

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
		State.NetSerialize(Ar, true);
	};

	Proxy.Deserialize();
	ProcessAuthorityState(State);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
	SimProxyRep.SerializeFunc = [&](FArchive& Ar) {
		WrappedState State;
		State.NetSerialize(Ar, false);

		ProcessAuthorityState(State);
	};
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::ProcessAuthorityState(const WrappedState& State) {
	if (State.FrameNumber == kInvalidFrame) {
		return;
	}

	WrappedState* ExistingState = States.FindByPredicate([&](const WrappedState& Candidate) { return Candidate.FrameNumber == State.FrameNumber; });
	if (ExistingState != nullptr) { return; }

	// Once CurrentFrame is set, it is assumed that all cues have been handled for that frame. So if this state is before that,
	// it needs to have the cues dispatched.
	if (CurrentFrame != kInvalidFrame && State.FrameNumber < CurrentFrame) {
		DispatchCues(State);
	}

	// Don't add any states that are "in the past"
	if (LastPoppedFrame != kInvalidFrame && State.FrameNumber <= LastPoppedFrame) {
		UE_LOG(LogTemp, Log, TEXT("Ignoring state that was in the past"));
		return;
	}

	States.Add(State);
	States.Sort([](const WrappedState& A, const WrappedState& B) {
		return A.FrameNumber < B.FrameNumber;
	});
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::DispatchCues(const WrappedState& State) {
	if (!State.Cues.IsEmpty()) {
		for (int Cue = 0; Cue < State.Cues.Num(); Cue++) {
			HandleCue(State.State, static_cast<CueSet>(State.Cues[Cue]));
		}
	}
}
