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

	virtual void Initialize() override;
	virtual void Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component) override;
	virtual ModelState GenerateOutput(Chaos::FReal Alpha) override;

	// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override;

private:

	virtual void Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component, bool bIsForcedSimulation);
	void ReconcileWithAuthority(Chaos::FReal Dt, UPrimitiveComponent* Component);

	void Rewind_Internal(const WrappedState& State, UPrimitiveComponent* Component);
	void ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component);

private:

	/** At this frame the authority and the auto proxy agreed */
	uint32 AckedFrame = kInvalidFrame;
	uint32 NextFrame = 0;

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
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Initialize() {
	GenerateInitialState(CurrentState.State);
	LastState = CurrentState.State;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component) {
	Tick(Dt, RemainingAccumulatedTime, Component, false);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Tick(Chaos::FReal Dt, Chaos::FReal RemainingAccumulatedTime, UPrimitiveComponent* Component, bool bIsForcedSimulation) {
	LastState = CurrentState.State;

	// Pre-tick
	CurrentState.FrameNumber = NextFrame++;
	CurrentState.RemainingAccumulatedTime = RemainingAccumulatedTime;
	CurrentState.Cues.Empty();

	if (!bIsForcedSimulation || InputBuffer.RemoteBufferSize() == 0) {

		// Generate a new input packet
		FInputPacketWrapper<InputPacket> Packet;
		Packet.PacketNumber = CurrentState.FrameNumber;
		InputDelegate.ExecuteIfBound(Packet.Packet, CurrentState.State, Dt);

		InputBuffer.QueueInputRemote(Packet);
		while (SlidingInputWindow.Num() >= kInputWindowSize) {
			SlidingInputWindow.Pop();
		}

		// Capture by value here so that the proxy stores the input packets with it
		SlidingInputWindow.Insert(Packet, 0);
		FNetSerializationProxy Proxy([=](FArchive& Ar) mutable {
			Ar << SlidingInputWindow;
		});

		EmitInputPackets.CheckCallable();
		EmitInputPackets(Proxy);
	}

	check(InputBuffer.ConsumeInputRemote(CurrentInputPacket));

	// Tick
	FSimulationOutput<ModelState, CueSet> Output(CurrentState);
	Simulate(Dt, Component, LastState, Output, CurrentInputPacket.Packet);

	for (int i = 0; i < CurrentState.Cues.Num(); i++) {
		HandleCue(CurrentState.State, static_cast<CueSet>(CurrentState.Cues[i]));
	}

	// Post-tick
	History.Enqueue(CurrentState);

	// If there are frames that are being used to fast-forward / resimulate no logic needs to be performed
	// for them
	if (!bIsForcedSimulation) {
		ReconcileWithAuthority(Dt, Component);
	}
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::ReconcileWithAuthority(Chaos::FReal Dt, UPrimitiveComponent* Component) {
	if (LastAuthorityState.FrameNumber == kInvalidFrame) {
		// Never received a frame from the server
		return;
	}

	if (LastAuthorityState.FrameNumber <= AckedFrame && AckedFrame != kInvalidFrame) {
		// Last state received from the server was already acknowledged
		return;
	}

	if (LastAuthorityState.FrameNumber > CurrentState.FrameNumber) {
		// TODO implement
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
			InputBuffer.Ack(LastAuthorityState.FrameNumber);
		} else {

			// Server / client mismatch. Resimulate the client
			FAnsiStringBuilderBase HistoricBuilder;
			HistoricState.Print(HistoricBuilder);
			const FString HistoricString = StringCast<TCHAR>(HistoricBuilder.ToString()).Get();

			FAnsiStringBuilderBase AuthorityBuilder;
			LastAuthorityState.Print(AuthorityBuilder);
			const FString AuthorityString = StringCast<TCHAR>(AuthorityBuilder.ToString()).Get();

			UE_LOG(LogTemp, Warning, TEXT("\nRewinding and resimulating from frame %i."), LastAuthorityState.FrameNumber);
			UE_LOG(LogTemp, Verbose, TEXT("Client\n%s\nAuthority\n%s\n"), *HistoricString, *AuthorityString);

			Rewind_Internal(LastAuthorityState, Component);
			ForceSimulate(InputBuffer.RemoteBufferSize(), Dt, Component);
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
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::BindToRepProxies(FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
	AutoProxyRep.SerializeFunc = [&](FArchive& Ar) {
		WrappedState State;
		State.NetSerialize(Ar);
		Ar << State.RemainingAccumulatedTime;

		if (LastAuthorityState.FrameNumber == kInvalidFrame || State.FrameNumber > LastAuthorityState.FrameNumber) {
			LastAuthorityState = State;

			// Figure out the time needed to adjust by to achieve desired offset. We use a full RTT, since the
			// server time is already RTT/2 seconds in the past
			GetRtt.CheckCallable();
			const Chaos::FReal TimeAheadOfAuthority = GetRtt() + kFixedDt;

			const Chaos::FReal FrameDelta = static_cast<Chaos::FReal>(CurrentState.FrameNumber) - static_cast<Chaos::FReal>(LastAuthorityState.FrameNumber) ;
			const Chaos::FReal CurrentDelta = FrameDelta * kFixedDt + (CurrentState.RemainingAccumulatedTime - LastAuthorityState.RemainingAccumulatedTime);
			const Chaos::FReal AdjustmentTime = TimeAheadOfAuthority - CurrentDelta;

			AdjustTime.CheckCallable();
			AdjustTime(AdjustmentTime);
		}
	};
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::Rewind_Internal(const WrappedState& State, UPrimitiveComponent* Component) {
	History.Empty();
	AckedFrame = State.FrameNumber;
	CurrentState = State;

	// Add here because the simulation is at State.FrameNumber so the next frame will be State.FrameNumber + 1
	NextFrame = State.FrameNumber + 1;

	InputBuffer.Rewind(AckedFrame);
	Rewind(State.State, Component);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void ClientPredictionAutoProxyDriver<InputPacket, ModelState, CueSet>::ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component) {
	for (uint32 i = 0; i < Ticks; i++) {
		Tick(TickDt, 0.0, Component, true);
	}
}
