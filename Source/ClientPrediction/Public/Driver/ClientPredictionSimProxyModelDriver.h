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
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveReliableAuthorityState(FNetSerializationProxy& Proxy) override;

	virtual void BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override;

private:

	uint32 CurrentFrame = kInvalidFrame;

	/**
	 *The frame number of the last state that was popped off the interpolation buffer.
	 * Anything before this is considered "in the past"
	 */
	uint32 LastPoppedFrame = kInvalidFrame;
	
	TArray<WrappedState> States;
	ModelState LastFinalizedState;

	/** The number of states to store before starting the interpolation */
	uint32 kTargetBufferSize = FMath::CeilToInt32(kDesiredInterpolationBufferMs / (kFixedDt * 1000.0) / kSyncFrames);
	
	uint32 StartInterpolationIndex = -1;
	uint32 EndInterpolationIndex = -1;
};

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	if (CurrentFrame == kInvalidFrame) {
		if (static_cast<uint32>(States.Num()) < kTargetBufferSize) {
			return;
		}
		
		CurrentFrame = States[0].FrameNumber;
	} else {
		CurrentFrame++;

		if (!States.IsEmpty()) {
			uint32 BufferSize = static_cast<uint32>(States.Num());

			// Dispatch cues if there are any
			for (uint32 i = 0; i < BufferSize; i++) {
				if (States[i].FrameNumber == CurrentFrame) {
					for (int Cue = 0; Cue < States[i].Cues.Num(); Cue++) {
						HandleCue(States[i].State, static_cast<CueSet>(States[i].Cues[Cue]));
					}
				}
			}
			
			// Find which two states to interpolate between.
			// If both indexes are the last element that means there was nothing to interpolate between and the latest
			// state should just be shown.
			StartInterpolationIndex = EndInterpolationIndex = BufferSize - 1;
		
			for (uint32 i = 0; i < BufferSize - 1; i++) {
				if (States[i].FrameNumber <= CurrentFrame
				 && i + 1 < BufferSize
				 && States[i + 1].FrameNumber > CurrentFrame) {
					StartInterpolationIndex = i;
					EndInterpolationIndex = i + 1;
					break;
				 }
			}

			// Pop states that aren't needed anymore
			if (StartInterpolationIndex != BufferSize - 1) {
				for (uint32 i = 0; i < StartInterpolationIndex; i++) {
					LastPoppedFrame = States[0].FrameNumber;
					States.RemoveAt(0);
				}
			}

			StartInterpolationIndex = 0;
			EndInterpolationIndex = 1;
			
		} else {
			StartInterpolationIndex = EndInterpolationIndex = kInvalidFrame;
		}
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState ClientPredictionSimProxyDriver<InputPacket, ModelState, CueSet>::GenerateOutput(Chaos::FReal Alpha) {
	if (States.IsEmpty() || StartInterpolationIndex == kInvalidFrame || EndInterpolationIndex == kInvalidFrame) {
		return LastFinalizedState;
	}

	if (StartInterpolationIndex == EndInterpolationIndex) {
		LastFinalizedState = States[StartInterpolationIndex].State;
	} else {
		const WrappedState Start = States[StartInterpolationIndex];
		const WrappedState End   = States[EndInterpolationIndex];

		float FrameDelta = static_cast<float>(End.FrameNumber - Start.FrameNumber);
		float FrameAlpha = static_cast<float>(CurrentFrame - Start.FrameNumber) / FrameDelta;
		float TrueAlpha  = FrameAlpha + Alpha / FrameDelta;
	
		LastFinalizedState = Start.State;
		LastFinalizedState.Interpolate(TrueAlpha, End.State);
	}
	
	return LastFinalizedState;
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
	if (LastPoppedFrame != kInvalidFrame && State.FrameNumber <= LastPoppedFrame) {
		if (!State.Cues.IsEmpty()) {
			for (int Cue = 0; Cue < State.Cues.Num(); Cue++) {
				HandleCue(State.State, static_cast<CueSet>(State.Cues[Cue]));
			}
		}
	}

	// This could potentially come out of order in relation to what comes out of the replication proxy,
	// so we sort it.
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

		States.Add(State);
	};
}
