﻿#pragma once

#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionModel.h"
#include "Driver/ClientPredictionRepProxy.h"

#include "ClientPredictionComponent.generated.h"

UCLASS( ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent) )
class CLIENTPREDICTION_API UClientPredictionComponent : public UActorComponent {

	GENERATED_BODY()

public:

	UClientPredictionComponent();
	virtual ~UClientPredictionComponent() override = default;

	virtual void InitializeComponent() override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void PreNetReceive() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	template <typename ModelType>
	ModelType* CreateModel();

private:

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);

	UFUNCTION(NetMulticast, Reliable)
	void RecvReliableAuthorityState(FNetSerializationProxy Proxy);

	float GetRtt() const;

	void CheckOwnerRoleChanged();

public:

	TUniquePtr<IClientPredictionModel> Model;

private:

	UPROPERTY(Replicated)
	FClientPredictionRepProxy AutoProxyRep;

	UPROPERTY(Replicated)
	FClientPredictionRepProxy SimProxyRep;

	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;

	ENetRole CachedRole = ENetRole::ROLE_None;
	bool bCachedAuthorityTakesInput = false;

};

template <typename ModelType>
ModelType* UClientPredictionComponent::CreateModel() {
	Model = MakeUnique<ModelType>();

	Model->EmitInputPackets = [&](FNetSerializationProxy& Proxy) {
		RecvInputPacket(Proxy);
	};

	Model->EmitReliableAuthorityState = [&](FNetSerializationProxy& Proxy) {
		RecvReliableAuthorityState(Proxy);
	};

	Model->GetRtt = [&]() { return GetRtt(); };
	return static_cast<ModelType*>(Model.Get());
}
