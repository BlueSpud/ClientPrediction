#include "ClientPredictionV2Component.h"

#include "Net/UnrealNetwork.h"

UClientPredictionV2Component::UClientPredictionV2Component() {
    SetIsReplicatedByDefault(true);
    bWantsInitializeComponent = true;

    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

UClientPredictionV2Component::~UClientPredictionV2Component() {}

void UClientPredictionV2Component::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(UClientPredictionV2Component, SimProxyStates, COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UClientPredictionV2Component, AutoProxyStates, COND_AutonomousOnly);
    DOREPLIFETIME(UClientPredictionV2Component, FinalState);
}

void UClientPredictionV2Component::InitializeComponent() {
    Super::InitializeComponent();

    UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
    check(UpdatedComponent);
}

void UClientPredictionV2Component::BeginPlay() {
    Super::BeginPlay();

    const AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr || SimCoordinator == nullptr) { return; }

    SimCoordinator->Initialize(UpdatedComponent, OwnerActor->GetLocalRole());

    if (FinalState.HasData()) {
        SimCoordinator->ConsumeFinalState(FinalState);
    }
}

void UClientPredictionV2Component::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    Super::EndPlay(EndPlayReason);
    DestroySimulation();
}

void UClientPredictionV2Component::UninitializeComponent() {
    Super::UninitializeComponent();
    DestroySimulation();
}

void UClientPredictionV2Component::DestroySimulation() {
    if (SimCoordinator != nullptr) {
        SimCoordinator->Destroy();

        SimCoordinator = nullptr;
        SimInput = nullptr;
        SimState = nullptr;
        SimEvents = nullptr;
    }
}

void UClientPredictionV2Component::ServerRecvInput_Implementation(const FBundledPackets& Bundle) {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeInputBundle(Bundle); }
}

void UClientPredictionV2Component::OnRep_SimProxyStates() {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeSimProxyStates(SimProxyStates); }
}

void UClientPredictionV2Component::OnRep_AutoProxyStates() {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeAutoProxyStates(AutoProxyStates); }
}

void UClientPredictionV2Component::OnRep_FinalState() {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeFinalState(FinalState); }
}

void UClientPredictionV2Component::ClientRecvEvents_Implementation(const FBundledPackets& Bundle) {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeEvents(Bundle); }
}

void UClientPredictionV2Component::ServerRecvRemoteSimProxyOffset_Implementation(const FRemoteSimProxyOffset& Offset) {
    if (SimCoordinator != nullptr) { SimCoordinator->ConsumeRemoteSimProxyOffset(Offset); }
}

bool UClientPredictionV2Component::ShouldSendToServer() const {
    return GetOwner() != nullptr && GetOwner()->GetNetConnection() != nullptr;
}
