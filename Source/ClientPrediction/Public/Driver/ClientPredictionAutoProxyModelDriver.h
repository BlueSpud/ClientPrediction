#pragma once

#include "ClientPredictionModelDriver.h"
#include "ClientPredictionModelTypes.h"

#include "../ClientPredictionNetSerialization.h"
#include "../Input.h"

template <typename InputPacket, typename ModelState, typename CueSet>
class ClientPredictionAutoProxyDriver : public IClientPredictionModelDriver<InputPacket, ModelState, CueSet> {
	using WrappedState = FModelStateWrapper<ModelState>;
	
public:

	ClientPredictionAutoProxyDriver() = default;

	// Simulation ticking
	
	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override;
	
private:

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, bool bIsForcedSimulation);
	
	void Rewind_Internal(const WrappedState& State, UPrimitiveComponent* Component);
	void ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component);
	
private:
	
	/** At this frame the authority and the auto proxy agreed */
	uint32 AckedFrame = kInvalidFrame;
	uint32 NextFrame = 0;
	uint32 NextInputPacket = 0;
	
	/** All of the frames that have not been reconciled with the authority. */
	TQueue<WrappedState> History;
	
	FInputPacketWrapper<InputPacket> CurrentInputPacket;

	WrappedState LastAuthorityState;
	WrappedState CurrentState;
	ModelState LastState;

	/* We send each input with several previous inputs. In case a packet is dropped, the next send will also contain the new dropped input */
	TArray<FInputPacketWrapper<InputPacket>> SlidingInputWindow;
	FInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;
};

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	Tick(Dt, Component, false);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, bool bIsForcedSimulation) {
	LastState = CurrentState.State;
	
	// Pre-tick
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.Cues.Empty();
	BeginTick(Dt, CurrentState.State, Component);
	
	if (!bIsForcedSimulation || InputBuffer.RemoteBufferSize() == 0) {
		FInputPacketWrapper<InputPacket> Packet;
		Packet.PacketNumber = NextInputPacket++;
		
		InputDelegate.ExecuteIfBound(Packet.Packet, CurrentState.State, Dt);
		InputBuffer.QueueInputRemote(Packet);

		EmitInputPackets.CheckCallable();

		if (SlidingInputWindow.Num() >= kInputWindowSize) {
			SlidingInputWindow.Pop();
		}
		
		// Capture by value here so that the proxy stores the input packets with it
		SlidingInputWindow.Insert(Packet, 0);
		FNetSerializationProxy Proxy([=](FArchive& Ar) mutable {
			Ar << SlidingInputWindow;
		});
		
		EmitInputPackets(Proxy);
	}
	
	check(InputBuffer.ConsumeInputRemote(CurrentInputPacket));
	CurrentState.InputPacketNumber = CurrentInputPacket.PacketNumber;
	
	// Tick
	FSimulationOutput<ModelState, CueSet> Output(CurrentState);
	Simulate(Dt, Component, LastState, Output, CurrentInputPacket.Packet);

	for (int i = 0; i < CurrentState.Cues.Num(); i++) {
		HandleCue(CurrentState.State, static_cast<CueSet>(CurrentState.Cues[i]));
	}

	// Post-tick
	History.Enqueue(CurrentState);
	 
	// If there are frames that are being used to fast-forward/resimulate no logic needs to be performed
	// for them
	if (bIsForcedSimulation) {
		return;
	}
	
	if (LastAuthorityState.FrameNumber == kInvalidFrame) {
		// Never received a frame from the server
		return;
	}

	if (LastAuthorityState.FrameNumber <= AckedFrame && AckedFrame != kInvalidFrame) {
		// Last state received from the server was already acknowledged
		return;
	}

	if (LastAuthorityState.InputPacketNumber == kInvalidFrame) {
		// Server has not started to consume input, ignore it since the client has been applying input since frame 0
		return;
	}
	
	if (LastAuthorityState.FrameNumber > CurrentState.FrameNumber) {
		// Server is ahead of the client. The client should just chuck out everything and resimulate
		UE_LOG(LogTemp, Warning, TEXT("Client was behind server. Jumping to frame %i and resimulating"), LastAuthorityState.FrameNumber);
		
		Rewind_Internal(LastAuthorityState, Component);
		ForceSimulate(FMath::Max(kClientForwardPredictionFrames, InputBuffer.RemoteBufferSize()), Dt, Component);
	} else {
		// Check history against the server state
		WrappedState HistoricState;
		bool bFound = false;
		
		while (!History.IsEmpty()) {
			History.Dequeue(HistoricState);
			if (HistoricState.FrameNumber == LastAuthorityState.FrameNumber) {
				bFound = true;
				break;
			}
		}

		check(bFound);

		if (HistoricState == LastAuthorityState) {
			// Server state and historic state matched, simulation was good up to LocalServerState.FrameNumber
			AckedFrame = LastAuthorityState.FrameNumber;
			InputBuffer.Ack(LastAuthorityState.InputPacketNumber);
			UE_LOG(LogTemp, Verbose, TEXT("Acked up to %i, input packet %i. Input buffer had %i elements"), AckedFrame, LastAuthorityState.InputPacketNumber, InputBuffer.RemoteBufferSize());
		} else {
			// Server/client mismatch. Resimulate the client
			FAnsiStringBuilderBase HistoricBuilder;
			HistoricState.Print(HistoricBuilder);
			FString HistoricString = StringCast<TCHAR>(HistoricBuilder.ToString()).Get();

			FAnsiStringBuilderBase AuthorityBuilder;
			LastAuthorityState.Print(AuthorityBuilder);
			FString AuthorityString = StringCast<TCHAR>(AuthorityBuilder.ToString()).Get();

			UE_LOG(LogTemp, Error, TEXT("======\nRewinding and resimulating from frame %i.\nClient\n%s\nAuthority\n%s\n======"), LastAuthorityState.FrameNumber, *HistoricString, *AuthorityString);
			
			Rewind_Internal(LastAuthorityState, Component);
			ForceSimulate(FMath::Max(kClientForwardPredictionFrames, InputBuffer.RemoteBufferSize()), Dt, Component);
		}
		
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
ModelState ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::GenerateOutput(Chaos::FReal Alpha) {
	ModelState InterpolatedState = LastState;
	InterpolatedState.Interpolate(Alpha, CurrentState.State);
	return InterpolatedState;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	// No-op since the client is the one sending the packets
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	WrappedState State;
	Proxy.NetSerializeFunc = [&State](FArchive& Ar) {
		State.NetSerialize(Ar);	
	};

	Proxy.Deserialize();
	if (LastAuthorityState.FrameNumber == kInvalidFrame || State.FrameNumber > LastAuthorityState.FrameNumber) {
		LastAuthorityState = State;
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Rewind_Internal(const WrappedState& State, UPrimitiveComponent* Component) {
	History.Empty();
	AckedFrame = State.FrameNumber;
	CurrentState = State;
	
	// Add here because the body is at State.FrameNumber so the next frame will be State.FrameNumber + 1
	NextFrame = State.FrameNumber + 1;

	InputBuffer.Rewind(State.InputPacketNumber);
	Rewind(State.State, Component);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component) {
	for (uint32 i = 0; i < Ticks; i++) {
		Tick(TickDt, Component, true);
	}
}
