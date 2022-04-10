#pragma once

#include "ClientPredictionNetSerialization.h"
#include "Input.h"
#include "Declares.h"
#include "Driver/ClientPredictionAuthorityModelDriver.h"

#include "Driver/ClientPredictionModelDriver.h"
#include "Driver/ClientPredictionModelTypes.h"

static constexpr uint32 kClientForwardPredictionFrames = 5;
static constexpr uint32 kAuthorityTargetInputBufferSize = 3;
static constexpr uint32 kInputWindowSize = 3;
static constexpr uint32 kSyncFrames = 5;

/**
 * The interface for the client prediction model. This exists so that the prediction component can hold a
 * reference to a templated model.
 */
class IClientPredictionModel {
	
public:

	IClientPredictionModel() = default;
	virtual ~IClientPredictionModel() = default;

	virtual void PreInitialize(ENetRole Role) = 0;
	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) = 0;

// Simulation ticking

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) = 0;

	/**
	 * To be called after ticks have been performed and finalizes the output from the model.
	 * @param Alpha the percentage that time is between the current tick and the next tick.
	 * @param Component The component that should be updated.
	 * @param Role The network role to finalize the output for */
	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) = 0;

// Input packet / state receiving

	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) = 0;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) = 0;
	
public:

	/** These are the functions to queue RPC sends. The proxies should use functions that capture by value */
	TFunction<void(FNetSerializationProxy&)> EmitInputPackets;
	TFunction<void(FNetSerializationProxy&)> EmitAuthorityState;
	
};

template <typename InputPacket, typename ModelState>
class BaseClientPredictionModel : public IClientPredictionModel {
	
public:

	BaseClientPredictionModel();
	virtual ~BaseClientPredictionModel() override = default;

	virtual void PreInitialize(ENetRole Role) override final;

	virtual void Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) override final;

	virtual void Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) override final;
	
	virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) override final;
	virtual void ReceiveAuthorityState(FNetSerializationProxy& Proxy) override final;

public:
	
	DECLARE_DELEGATE_OneParam(FInputProductionDelgate, InputPacket&)
	FInputProductionDelgate InputDelegate;
	
protected:

	void Tick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role);

	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) = 0;
	virtual void Rewind(const ModelState& State, UPrimitiveComponent* Component) = 0;

	virtual void ApplyState(UPrimitiveComponent* Component, const ModelState& State);
	
private:
	
	void TickAutoProxy(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component);
	void TickSimProxy(Chaos::FReal Dt, UPrimitiveComponent* Component);
	
	void PostTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role);
	void PostTickAutoProxy(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component);

	void Rewind_Internal(const FModelStateWrapper<ModelState>& State, UPrimitiveComponent* Component);
	
	void ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component);
	
private:
	
	/**
	 * If this object belongs to a client, the last acknowledged frame from the server.
	 * At this frame the client was identical to the server. 
	 */
	uint32 AckedServerFrame = kInvalidFrame;

	/** The index of the next frame on both the remotes and authority */
	uint32 NextLocalFrame = 0;

	/** Autonomous proxy index for the next input packet number */
	uint32 NextInputPacket = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacketIdx = kInvalidFrame;
	FInputPacketWrapper<InputPacket> CurrentInputPacket;
	
	/** On the client this is all of the frames that have not been reconciled with the server. */
	TQueue<FModelStateWrapper<ModelState>> ClientHistory;
	
	/**
	 * The last state that was received from the authority.
	 * We use atomic here because the state will be written to from whatever thread handles receiving the state
	 * from the authority and be read from the physics thread.
	 */
	FModelStateWrapper<ModelState> LastAuthorityState;
	FModelStateWrapper<ModelState> CurrentState;
	ModelState LastState;

	FInputBuffer<FInputPacketWrapper<InputPacket>> InputBuffer;

	/* We send each input with several previous inputs. In case a packet is dropped, the next send will also contain the new dropped input */
	TArray<FInputPacketWrapper<InputPacket>> SlidingInputWindow;

	TUniquePtr<IClientPredictionModelDriver<InputPacket, ModelState>> Driver;

};

template <typename InputPacket, typename ModelState>
BaseClientPredictionModel<InputPacket, ModelState>::BaseClientPredictionModel() {
	InputBuffer.SetAuthorityTargetBufferSize(kAuthorityTargetInputBufferSize);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ReceiveInputPackets(FNetSerializationProxy& Proxy) {
	Driver->ReceiveInputPackets(Proxy);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ReceiveAuthorityState(FNetSerializationProxy& Proxy) {
	FModelStateWrapper<ModelState> State;
	Proxy.NetSerializeFunc = [&State](FArchive& Ar) {
		State.NetSerialize(Ar);	
	};

	Proxy.Deserialize();
	LastAuthorityState = State;
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::PreInitialize(ENetRole Role) {
	Driver = MakeUnique<ClientPredictionAuthorityDriver<InputPacket, ModelState>>();

	Driver->EmitInputPackets = EmitInputPackets;
	Driver->EmitAuthorityState = EmitAuthorityState;
	Driver->Simulate = [&](Chaos::FReal Dt, UPrimitiveComponent* Component, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) {
		Simulate(Dt, Component, PrevState, OutState, Input);
	};
	
	Driver->Rewind = [&](const ModelState& State, UPrimitiveComponent* Component) {
		Rewind(State, Component);
	};
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Tick(Chaos::FReal Dt, UPrimitiveComponent* Component, ENetRole Role) {
	if (Role == ENetRole::ROLE_Authority) {
		Driver->Tick(Dt, Component);	
	} else {
		Tick(Dt, false, Component, Role);
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Tick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) {
	LastState = CurrentState.State;

	switch (Role) {
	case ENetRole::ROLE_AutonomousProxy:
		TickAutoProxy(Dt, bIsForcedSimulation, Component);
		break;
	case ENetRole::ROLE_SimulatedProxy:
		TickSimProxy(Dt, Component);
		break;
	default:
		return;
	}
	
	PostTick(Dt, bIsForcedSimulation, Component, Role);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::TickAutoProxy(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component) {
	if (!bIsForcedSimulation || InputBuffer.RemoteBufferSize() == 0) {
		FInputPacketWrapper<InputPacket> Packet;
		Packet.PacketNumber = NextInputPacket++;
		
		InputDelegate.ExecuteIfBound(Packet.Packet);
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
	CurrentInputPacketIdx = CurrentInputPacket.PacketNumber;
	
	CurrentState = FModelStateWrapper<ModelState>();
	Simulate(Dt, Component, LastState, CurrentState.State, CurrentInputPacket.Packet);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::TickSimProxy(Chaos::FReal Dt, UPrimitiveComponent* Component) {

	// TODO interpolate from a snapshot buffer
	CurrentState = LastAuthorityState;
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::PostTick(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component, ENetRole Role) {
	CurrentState.FrameNumber = NextLocalFrame++;
	CurrentState.InputPacketNumber = CurrentInputPacketIdx;
	
	switch (Role) {
	case ENetRole::ROLE_AutonomousProxy:
		PostTickAutoProxy(Dt, bIsForcedSimulation, Component);
		break;
	default:
		return;
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::PostTickAutoProxy(Chaos::FReal Dt, bool bIsForcedSimulation, UPrimitiveComponent* Component) {
	ClientHistory.Enqueue(CurrentState);

	// If there are frames that are being used to fast-forward/resimulate no logic needs to be performed
	// for them
	if (bIsForcedSimulation) {
		return;
	}
	
	if (LastAuthorityState.FrameNumber == kInvalidFrame) {
		// Never received a frame from the server
		return;
	}

	if (LastAuthorityState.FrameNumber <= AckedServerFrame && AckedServerFrame != kInvalidFrame) {
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
		FModelStateWrapper<ModelState> HistoricState;
		bool bFound = false;
		
		while (!ClientHistory.IsEmpty()) {
			ClientHistory.Dequeue(HistoricState);
			if (HistoricState.FrameNumber == LastAuthorityState.FrameNumber) {
				bFound = true;
				break;
			}
		}

		check(bFound);

		if (HistoricState == LastAuthorityState) {
			// Server state and historic state matched, simulation was good up to LocalServerState.FrameNumber
			AckedServerFrame = LastAuthorityState.FrameNumber;
			InputBuffer.Ack(LastAuthorityState.InputPacketNumber);
			UE_LOG(LogTemp, Verbose, TEXT("Acked up to %i, input packet %i. Input buffer had %i elements"), AckedServerFrame, LastAuthorityState.InputPacketNumber, InputBuffer.RemoteBufferSize());
		} else {
			// Server/client mismatch. Resimulate the client
			UE_LOG(LogTemp, Error, TEXT("Rewinding and resimulating from frame %i which used input packet %i"), LastAuthorityState.FrameNumber, LastAuthorityState.InputPacketNumber);

			Rewind_Internal(LastAuthorityState, Component);
			ForceSimulate(FMath::Max(kClientForwardPredictionFrames, InputBuffer.RemoteBufferSize()), Dt, Component);
		}
		
	}
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ApplyState(UPrimitiveComponent* Component, const ModelState& State) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Finalize(Chaos::FReal Alpha, UPrimitiveComponent* Component, ENetRole Role) {
	if (Role == ENetRole::ROLE_Authority) {
		ModelState InterpolatedState = Driver->GenerateOutput(Alpha);
		ApplyState(Component, InterpolatedState);
		return;
	}
	
	ModelState InterpolatedState = LastState;
	InterpolatedState.Interpolate(Alpha, CurrentState.State);
	ApplyState(Component, InterpolatedState);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::Rewind_Internal(const FModelStateWrapper<ModelState>& State, UPrimitiveComponent* Component) {
	ClientHistory.Empty();
	AckedServerFrame = State.FrameNumber;
	
	// Add here because the body is at State.FrameNumber so the next frame will be State.FrameNumber + 1
	NextLocalFrame = State.FrameNumber + 1;

	InputBuffer.Rewind(State.InputPacketNumber);
	CurrentInputPacketIdx = State.InputPacketNumber;

	Rewind(State.State, Component);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionModel<InputPacket, ModelState>::ForceSimulate(uint32 Ticks, Chaos::FReal TickDt, UPrimitiveComponent* Component) {
	for (uint32 i = 0; i < Ticks; i++) {
		Tick(TickDt, true, Component, ENetRole::ROLE_AutonomousProxy);
	}
}
