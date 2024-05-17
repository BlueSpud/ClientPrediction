#pragma once

#include "ClientPredictionSimCoordinator.h"
#include "ClientPredictionSimInput.h"

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
    ClientPrediction::FSimDelegates<Traits>& CreateSimulation();

private:
    void DestroySimulation();

    UFUNCTION(Server, Unreliable)
    void ServerRecvInput(const ClientPrediction::FInputBundle& Bundle);

    UPROPERTY()
    class UPrimitiveComponent* UpdatedComponent;

    TUniquePtr<ClientPrediction::USimCoordinatorBase> SimCoordinator;
    TSharedPtr<ClientPrediction::USimInputBase> SimInput;
};

template <typename Traits>
ClientPrediction::FSimDelegates<Traits>& UClientPredictionV2Component::CreateSimulation() {
    TSharedPtr<ClientPrediction::USimInput<Traits>> InputImpl = MakeShared<ClientPrediction::USimInput<Traits>>();
    SimInput = InputImpl;

    TUniquePtr<ClientPrediction::USimCoordinator<Traits>> Impl = MakeUnique<ClientPrediction::USimCoordinator<Traits>>(InputImpl);
    SimCoordinator = MoveTemp(Impl);

    return Impl->GetSimDelegates();
}
