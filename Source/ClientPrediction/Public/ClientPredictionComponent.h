#pragma once

#include "ClientPredictionNetSerialization.h"
#include "Driver/ClientPredictionRepProxy.h"

#include "V2/Physics/ClientPredictionPhysicsModelV2.h"

#include "ClientPredictionComponent.generated.h"

UCLASS( ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent) )
class CLIENTPREDICTION_API UClientPredictionComponent : public UActorComponent, public ClientPrediction::IPhysicsModelDelegate {

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

private:

	virtual void EmitInputPackets(FNetSerializationProxy& Proxy) override;

	UFUNCTION(Server, Unreliable)
	void RecvInputPacket(FNetSerializationProxy Proxy);

	UFUNCTION(NetMulticast, Reliable)
	void RecvReliableAuthorityState(FNetSerializationProxy Proxy);

private:

	UPROPERTY(Replicated)
	FClientPredictionRepProxy AutoProxyRep;

	UPROPERTY(Replicated)
	FClientPredictionRepProxy SimProxyRep;

	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;

	ENetRole CachedRole = ENetRole::ROLE_None;
	uint8 bCachedAuthorityTakesInput = -1;

	ClientPrediction::FPhysicsModel TestPhysicsModel;

};