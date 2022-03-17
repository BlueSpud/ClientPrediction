#pragma once

#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionPhysics.h"
#include "Input.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"

#include "ClientPredictionComponent.generated.h"

UCLASS( ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent) )
class CLIENTPREDICTION_API UClientPredictionComponent : public UActorComponent {

	GENERATED_BODY()

public:

	UClientPredictionComponent();
	virtual ~UClientPredictionComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	template <typename ModelType>
	ModelType* CreateModel();
	
public:

	TUniquePtr<IClientPredictionModel> Model;
	
protected:
	
	virtual void OnRegister() override;

private:

	void PrePhysicsAdvance(Chaos::FReal Dt);
	void OnPhysicsAdvanced(Chaos::FReal Dt);
	void ForceSimulate(uint32 Frames);
	
	UFUNCTION(NetMulticast, Unreliable)
	void RecvServerState(FNetSerializationProxy Proxy);

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);


private:

	/**
	 * The timestep for each frame. It is expected that this is always constant and the server and client
	 * are using the exact same timestep. Async physics should be enabled.
	 */
	float Timestep = 0.0;
	

	/** RPC's cannot be called on the physics thread. This is the queued states to send to the client from the game thread. */
	TQueue<FNetSerializationProxy> QueuedClientSendStates;
	
	/** The inputs to send to the server (sending must be called from the game thread). */
	TQueue<FNetSerializationProxy> InputBufferSendQueue;

	FDelegateHandle PrePhysicsAdvancedDelegate;
	FDelegateHandle OnPhysicsAdvancedDelegate;
	
	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;
	
	ImmediatePhysics::FSimulation* PhysicsSimulation = nullptr;
	ImmediatePhysics::FActorHandle* UpdatedComponentPhysicsHandle = nullptr;

	float AccumulatedTime = 0.0;
	bool bIsForceSimulating = false;
	
};

template <typename ModelType>
ModelType* UClientPredictionComponent::CreateModel() {
	Model = MakeUnique<ModelType>();
	Model->ForceSimulate = [&](uint32 Frames) {
		ForceSimulate(Frames);
	};

	Model->EmitInputPackets = [&](FNetSerializationProxy& Proxy) {
		InputBufferSendQueue.Enqueue(Proxy);
	};

	Model->EmitAuthorityState = [&](FNetSerializationProxy& Proxy) {
		QueuedClientSendStates.Enqueue(Proxy);
	};

	return static_cast<ModelType*>(Model.Get());
}
