#pragma once

#include "ClientPredictionSimCoordinator.h"

#include "ClientPredictionV2Component.generated.h"

UCLASS(ClassGroup=(ClientPrediction), meta=(BlueprintSpawnableComponent))
class CLIENTPREDICTION_API UClientPredictionV2Component : public UActorComponent {
    GENERATED_BODY()

public:
    UClientPredictionV2Component();
    virtual ~UClientPredictionV2Component() override;

    virtual void InitializeComponent() override;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void UninitializeComponent() override;

    template <typename Traits>
    void CreateSimulation();

private:
    void DestroySimulation();

    UPROPERTY()
    class UPrimitiveComponent* UpdatedComponent;
    TUniquePtr<ClientPrediction::USimCoordinatorBase> SimCoordinator;
};

template <typename Traits>
void UClientPredictionV2Component::CreateSimulation() {
    SimCoordinator = MakeUnique<ClientPrediction::USimCoordinator<Traits>>();
}
