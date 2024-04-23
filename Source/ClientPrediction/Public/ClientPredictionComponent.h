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
    virtual ~UClientPredictionComponent() override;

    virtual void InitializeComponent() override;
    virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
    virtual void PreNetReceive() override;
    void CheckOwnerRoleChanged();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void UninitializeComponent() override;

    template <typename ModelType>
    ModelType* CreateModel();

private:
    void DestroyModel();

private:
    virtual void EmitInputPackets(FNetSerializationProxy& Proxy) override;
    virtual void GetNetworkConditions(ClientPrediction::FNetworkConditions& NetworkConditions) const override;

    UFUNCTION(Server, Unreliable)
    void RecvInputPacket(FNetSerializationProxy Proxy);

private:
    UPROPERTY(Replicated)
    FRepProxy ControlRepProxy;

    UPROPERTY(Replicated)
    FRepProxy FinalStateRepProxy;

    UPROPERTY()
    class UPrimitiveComponent* UpdatedComponent;

    ENetRole CachedRole = ENetRole::ROLE_None;
    uint8 bCachedAuthorityTakesInput = -1;

    TUniquePtr<ClientPrediction::FPhysicsModelBase> PhysicsModel;
};

template <typename ModelType>
ModelType* UClientPredictionComponent::CreateModel() {
    check(PhysicsModel == nullptr);

    ModelType* Model = new ModelType();
    Model->SetModelId(FClientPredictionModelId(GetOwner()));

    PhysicsModel = TUniquePtr<ModelType>(Model);

    return Model;
}
