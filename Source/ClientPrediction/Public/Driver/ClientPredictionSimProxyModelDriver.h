#pragma once

#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"

template <typename InputPacket, typename ModelState>
class ClientPredictionSimProxyDriver : public IClientPredictionModelDriver<InputPacket, ModelState> {

public:

	ClientPredictionSimProxyDriver() = default;

	// Simulation ticking
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override;

private:

	uint32 CurrentFrame = kInvalidFrame;

	/**
	 *The frame number of the last state that was popped off the interpolation buffer.
	 * Anything before this is considered "in the past"
	 */
	uint32 LastPoppedFrame = kInvalidFrame;
	
	TArray<FModelStateWrapper<ModelState>> States;
	ModelState LastFinalizedState;

	/** The number of states to store before starting the interpolation */
	uint32 kTargetBufferSize = FMath::CeilToInt32(kDesiredInterpolationBufferMs / (kFixedDt * 1000.0) / kSyncFrames);
	
	uint32 StartInterpolationIndex = -1;
	uint32 EndInterpolationIndex = -1;
};

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	if (CurrentFrame == kInvalidFrame) {
		if (static_cast<uint32>(States.Num()) < kTargetBufferSize) {
			return;
		}
		
		CurrentFrame = States[0].FrameNumber;
	} else {
		CurrentFrame++;

		if (!States.IsEmpty()) {
			uint32 BufferSize = static_cast<uint32>(States.Num());
			
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

template <typename InputPacket, typename ModelState>
ModelState ClientPredictionSimProxyDriver<InputPacket, ModelState>::GenerateOutput(Chaos::FReal Alpha) {
	if (States.IsEmpty() || StartInterpolationIndex == kInvalidFrame || EndInterpolationIndex == kInvalidFrame) {
		return LastFinalizedState;
	}

	if (StartInterpolationIndex == EndInterpolationIndex) {
		LastFinalizedState = States[StartInterpolationIndex].State;
	} else {
		const FModelStateWrapper<ModelState> Start = States[StartInterpolationIndex];
		const FModelStateWrapper<ModelState> End   = States[EndInterpolationIndex];

		float FrameDelta = static_cast<float>(End.FrameNumber - Start.FrameNumber);
		float FrameAlpha = static_cast<float>(CurrentFrame - Start.FrameNumber) / FrameDelta;
		float TrueAlpha  = FrameAlpha + Alpha / FrameDelta;
	
		LastFinalizedState = Start.State;
		LastFinalizedState.Interpolate(TrueAlpha, End.State);
	}
	
	return LastFinalizedState;
}

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	// No-op, sim proxy should never get input
}

template <typename InputPacket, typename ModelState>
void ClientPredictionSimProxyDriver<InputPacket, ModelState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	FModelStateWrapper<ModelState> State;
	Proxy.NetSerializeFunc = [&State](FArchive& Ar) {
		State.NetSerialize(Ar);	
	};

	Proxy.Deserialize();

	// Check if the state already exists in the buffer
	bool bIsDuplicate = States.ContainsByPredicate([&](const FModelStateWrapper<ModelState>& Candidate) {
		return Candidate.FrameNumber == State.FrameNumber;
	});
	
	if (bIsDuplicate) {
		return;
	}

	// Don't add any states that are "in the past"
	if (LastPoppedFrame != kInvalidFrame && State.FrameNumber <= LastPoppedFrame) {
		return;
	}

	States.Add(State);
	
	// States aren't guaranteed to come in order so they need to be sorted every time.
	States.Sort([](const FModelStateWrapper<ModelState>& A, const FModelStateWrapper<ModelState>& B) {
		return A.FrameNumber < B.FrameNumber;
	});
}
