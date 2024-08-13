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

    UPROPERTY(ReplicatedUsing=OnRep_FinalState, Transient)
    FBundledPacketsFull FinalState;

    UFUNCTION()
    void OnRep_SimProxyStates();

    UFUNCTION()
    void OnRep_AutoProxyStates();

    UFUNCTION()
    void OnRep_FinalState();

    UFUNCTION(NetMulticast, Reliable)
    void ClientRecvEvents(const FBundledPackets& Bundle);

    UFUNCTION(Server, Reliable)
    void ServerRecvRemoteSimProxyOffset(const FRemoteSimProxyOffset& Offset);

    bool ShouldSendToServer() const;

    UPROPERTY()
    class UPrimitiveComponent* UpdatedComponent;

    TSharedPtr<ClientPrediction::USimInputBase> SimInput;
    TSharedPtr<ClientPrediction::USimStateBase> SimState;
    TSharedPtr<ClientPrediction::USimEvents> SimEvents;
    TUniquePtr<ClientPrediction::USimCoordinatorBase> SimCoordinator;
};

template <typename Traits>
TSharedPtr<ClientPrediction::FSimDelegates<Traits>> UClientPredictionV2Component::CreateSimulation() {
    TSharedPtr<ClientPrediction::USimInput<Traits>> InputImpl = MakeShared<ClientPrediction::USimInput<Traits>>();
    TSharedPtr<ClientPrediction::USimState<Traits>> StateImpl = MakeShared<ClientPrediction::USimState<Traits>>();
    SimEvents = MakeShared<ClientPrediction::USimEvents>();

    TUniquePtr<ClientPrediction::USimCoordinator<Traits>> Impl = MakeUnique<ClientPrediction::USimCoordinator<Traits>>(InputImpl, StateImpl, SimEvents);
    TSharedPtr<ClientPrediction::FSimDelegates<Traits>> Delegates = Impl->GetSimDelegates();

    InputImpl->EmitInputBundleDelegate.BindWeakLambda(this, [&](const FBundledPackets& Bundle) {
        if (!ShouldSendToServer()) { return; }
        ServerRecvInput(Bundle);
    });


    StateImpl->EmitSimProxyBundle.BindLambda([&](const FBundledPacketsLow& Packets) { SimProxyStates.Bundle().Copy(Packets.Bundle()); });
    StateImpl->EmitAutoProxyBundle.BindLambda([&](const FBundledPacketsFull& Packets) { AutoProxyStates.Bundle().Copy(Packets.Bundle()); });
    StateImpl->EmitFinalBundle.BindLambda([&](const FBundledPacketsFull& Packets) { FinalState.Bundle().Copy(Packets.Bundle()); });

    SimEvents->EmitEventBundle.BindUFunction(this, TEXT("ClientRecvEvents"));

    Impl->RemoteSimProxyOffsetChangedDelegate.BindWeakLambda(this, [&](const FRemoteSimProxyOffset& Offset) {
        if (!ShouldSendToServer()) { return; }
        ServerRecvRemoteSimProxyOffset(Offset);
    });

    SimInput = MoveTemp(InputImpl);
    SimState = MoveTemp(StateImpl);

    SimCoordinator = MoveTemp(Impl);
    return Delegates;
}
