#pragma once

#include "ClientPredictionNetSerialization.h"
#include "Driver/ClientPredictionRepProxy.h"
#include "Data/ClientPredictionModelId.h"

#include "Physics/ClientPredictionPhysicsModel.h"

#include "ClientPredictionComponent.generated.h"

UCLASS(ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent))
class CLIENTPREDICTION_API UClientPredictionComponent : public UActorComponent,
                                                        public ClientPrediction::IPhysicsModelDelegate {
	GENERATED_BODY()

public:
	UClientPredictionComponent();
	virtual ~UClientPredictionComponent() override = default;

	virtual void InitializeComponent() override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void PreNetReceive() override;
	void CheckOwnerRoleChanged();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	template <typename ModelType>
	ModelType* CreateModel();

private:
	virtual void EmitInputPackets(FNetSerializationProxy& Proxy) override;
	virtual void EmitReliableAuthorityState(FNetSerializationProxy& Proxy) override;
	virtual void GetNetworkConditions(ClientPrediction::FNetworkConditions& NetworkConditions) const override;

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);

	UFUNCTION(NetMulticast, Reliable)
	void RecvReliableAuthorityState(FNetSerializationProxy Proxy);

private:
	UPROPERTY(Replicated)
	FRepProxy AutoProxyRep;

	UPROPERTY(Replicated)
	FRepProxy ControlProxyRep;

	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;

	ENetRole CachedRole = ENetRole::ROLE_None;
	uint8 bCachedAuthorityTakesInput = -1;

	TUniquePtr<ClientPrediction::FPhysicsModelBase> PhysicsModel;
};

template <typename ModelType>
ModelType* UClientPredictionComponent::CreateModel() {
	ModelType* Model = new ModelType();
	Model->SetModelId(FClientPredictionModelId(GetOwner()));

	PhysicsModel = TUniquePtr<ModelType>(Model);

	return Model;
}
