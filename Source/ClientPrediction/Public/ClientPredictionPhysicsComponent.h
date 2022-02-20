#pragma once

#include <atomic>

#include "ClientPredictionNetSerialization.h"
#include "Input.h"
#include "PhysicsState.h"

#include "ClientPredictionPhysicsComponent.generated.h"

UCLASS( ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent) )
class CLIENTPREDICTION_API UClientPredictionPhysicsComponent : public UActorComponent {

	GENERATED_BODY()

public:

	UClientPredictionPhysicsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	
	/** The number of frames before the client will send an update to the server. */
	UPROPERTY(EditAnywhere)
	uint32 SyncFrames = 5;

	/** The desired number of frames ahead that the client should be */
	UPROPERTY(EditAnywhere)
	uint32 ClientForwardPredictionFrames = 5;

	DECLARE_DELEGATE_OneParam(FInputProductionDelgate, FInputPacket&)
	FInputProductionDelgate InputDelegate;
	
protected:
	virtual void OnRegister() override;

private:

	void PrePhysicsAdvance(Chaos::FReal Dt);
	void PrePhysicsAdvanceAutonomousProxy();
	void PrePhysicsAdvanceAuthority();
	
	void OnPhysicsAdvanced(Chaos::FReal Dt);
	void OnPhysicsAdvancedAutonomousProxy();
	void OnPhysicsAdvancedAuthority();

	void Rewind(FPhysicsState& State, Chaos::FRigidBodyHandle_Internal* Handle);
	void ForceSimulate(uint32 Frames);
	
	UFUNCTION(Client, Unreliable)
	void RecvServerState(FPhysicsState State);

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);


private:
	
	/**
	 * If this object belongs to a client, the last acknowledged frame from the server.
	 * At this frame the client was identical to the server. 
	 */
	uint32 AckedServerFrame = FPhysicsState::kInvalidFrame;
	
	uint32 NextLocalFrame = 0;

	/** Client index for the next input packet number */
	uint32 NextInputPacket = 0;

	/** Input packet used for the current frame */
	uint32 CurrentInputPacket = FPhysicsState::kInvalidFrame;

	/**
	 * Resimulations are queued from the physics thread, so we cannot block on the resimulation (otherwise deadlock).
	 * This keeps track of how many frames are queued for resimulation.
	 */
	uint32 ForceSimulationFrames = 0;

	/**
	 * The timestep for each frame. It is expected that this is always constant and the server and client
	 * are using the exact same timestep. Async physics should be enabled.
	 */
	float Timestep = 0.0;

	/** On the client this is all of the frames that have not been reconciled with the server. */
	TQueue<FPhysicsState> ClientHistory;

	/**
	 * The last state that was received from the server.
	 * We use atomic here because the state will be written to from whatever thread handles receiving the state
	 * from the server and be read from the physics thread.
	 */
	std::atomic<FPhysicsState> LastServerState;

	/** RPC's cannot be called on the physics thread. This is the queued states to send to the client from the game thread. */
	TQueue<FPhysicsState> QueuedClientSendState;

	FInputBuffer<FInputPacket> InputBuffer;
	
	/** The inputs to send to the server (sending must be called from the game thread). */
	TQueue<FInputPacket> InputBufferSendQueue;

	FDelegateHandle PrePhysicsAdvancedDelegate;
	FDelegateHandle OnPhysicsAdvancedDelegate;
	
	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;
};