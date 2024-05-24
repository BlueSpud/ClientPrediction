#pragma once

#include "ClientPredictionSimCoordinator.h"
#include "ClientPredictionSimInput.h"
#include "ClientPredictionSimState.h"
#include "ClientPredictionNetSerialization.h"

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
    TSharedPtr<ClientPrediction::FSimDelegates<Traits>> CreateSimulation();

private:
    void DestroySimulation();

    UFUNCTION(Server, Unreliable)
    void ServerRecvInput(const FBundledPackets& Bundle);

    UPROPERTY(ReplicatedUsing=OnRep_SimProxyStates, Transient)
    FBundledPacketsLow SimProxyStates;

    UPROPERTY(ReplicatedUsing=OnRep_AutoProxyStates, Transient)
    FBundledPacketsFull AutoProxyStates;

    UFUNCTION()
    void OnRep_SimProxyStates();

    UFUNCTION()
    void OnRep_AutoProxyStates();

    UPROPERTY()
    class UPrimitiveComponent* UpdatedComponent;

    TSharedPtr<ClientPrediction::USimInputBase> SimInput;
    TSharedPtr<ClientPrediction::USimStateBase> SimState;
    TSharedPtr<ClientPrediction::USimEventsBase> SimEvents;
    TUniquePtr<ClientPrediction::USimCoordinatorBase> SimCoordinator;
};

template <typename Traits>
TSharedPtr<ClientPrediction::FSimDelegates<Traits>> UClientPredictionV2Component::CreateSimulation() {
    TSharedPtr<ClientPrediction::USimInput<Traits>> InputImpl = MakeShared<ClientPrediction::USimInput<Traits>>();
    TSharedPtr<ClientPrediction::USimState<Traits>> StateImpl = MakeShared<ClientPrediction::USimState<Traits>>();
    TSharedPtr<ClientPrediction::USimEvents<Traits>> EventsImpl = MakeShared<ClientPrediction::USimEvents<Traits>>();

    TUniquePtr<ClientPrediction::USimCoordinator<Traits>> Impl = MakeUnique<ClientPrediction::USimCoordinator<Traits>>(InputImpl, StateImpl, EventsImpl);
    TSharedPtr<ClientPrediction::FSimDelegates<Traits>> Delegates = Impl->GetSimDelegates();

    InputImpl->EmitInputBundleDelegate.BindUFunction(this, TEXT("ServerRecvInput"));
    StateImpl->EmitSimProxyBundle.BindLambda([&](const FBundledPacketsLow& Packets) { SimProxyStates.Bundle().Copy(Packets.Bundle()); });
    StateImpl->EmitAutoProxyBundle.BindLambda([&](const FBundledPacketsFull& Packets) { AutoProxyStates.Bundle().Copy(Packets.Bundle()); });

    SimInput = MoveTemp(InputImpl);
    SimState = MoveTemp(StateImpl);
    SimCoordinator = MoveTemp(Impl);

    return Delegates;
}
