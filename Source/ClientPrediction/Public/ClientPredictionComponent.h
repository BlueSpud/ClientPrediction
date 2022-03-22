#pragma once

#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionModel.h"
#include "Input.h"

#include "ClientPredictionComponent.generated.h"

UCLASS( ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent) )
class CLIENTPREDICTION_API UClientPredictionComponent : public UActorComponent {

	GENERATED_BODY()

public:

	UClientPredictionComponent();
	virtual ~UClientPredictionComponent() override = default;

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
	
	UFUNCTION(NetMulticast, Unreliable)
	void RecvServerState(FNetSerializationProxy Proxy);

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);


private:
	

	/** RPC's cannot be called on the physics thread. This is the queued states to send to the client from the game thread. */
	TQueue<FNetSerializationProxy> QueuedClientSendStates;
	
	/** The inputs to send to the server (sending must be called from the game thread). */
	TQueue<FNetSerializationProxy> InputBufferSendQueue;
	
	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;

	float AccumulatedTime = 0.0;
	
};

template <typename ModelType>
ModelType* UClientPredictionComponent::CreateModel() {
	Model = MakeUnique<ModelType>();

	Model->EmitInputPackets = [&](FNetSerializationProxy& Proxy) {
		InputBufferSendQueue.Enqueue(Proxy);
	};

	Model->EmitAuthorityState = [&](FNetSerializationProxy& Proxy) {
		QueuedClientSendStates.Enqueue(Proxy);
	};

	return static_cast<ModelType*>(Model.Get());
}
