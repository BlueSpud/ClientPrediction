#pragma once

#include <atomic>

#include "PhysicsState.h"

#include "PhysicsComponent.generated.h"

UCLASS(BlueprintType)
class CLIENTPREDICTION_API UPhysicsComponent : public UActorComponent {

	GENERATED_BODY()

public:

	/** The number of frames before the client will send an update to the server. */
	UPROPERTY(EditAnywhere)
	uint32 SyncFrames = 5;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
protected:
	virtual void OnRegister() override;

private:

	void OnPhysicsAdvanced(float Dt);
	void OnPhysicsAdvancedAutonomousProxy();
	void OnPhysicsAdvancedAuthority();

	UFUNCTION(Client, Unreliable)
	void RecvServerState(FPhysicsState State);

private:

	static constexpr uint32 kInvalidFrame = -1;
	
	/**
	 * If this object belongs to a client, the last acknowledged frame from the server.
	 * At this frame the client was identical to the server. 
	 */
	uint32 AckedServerFrame = kInvalidFrame;
	uint32 NextLocalFrame = 0;

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
	FDelegateHandle OnPhysicsAdvancedDelegate;
	
	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;
};