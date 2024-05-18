#include "ClientPredictionV2Component.h"

UClientPredictionV2Component::UClientPredictionV2Component() {
    SetIsReplicatedByDefault(true);
    bWantsInitializeComponent = true;

    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

UClientPredictionV2Component::~UClientPredictionV2Component() {}

void UClientPredictionV2Component::InitializeComponent() {
    Super::InitializeComponent();

    UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
    check(UpdatedComponent);
}

void UClientPredictionV2Component::BeginPlay() {
    Super::BeginPlay();

    const AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr) { return; }

    if (SimCoordinator != nullptr) {
        SimCoordinator->Initialize(UpdatedComponent, OwnerActor->GetNetConnection() != nullptr, OwnerActor->GetLocalRole());
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
    }
}

void UClientPredictionV2Component::ServerRecvInput_Implementation(const FInputBundle& Bundle) {
    if (SimInput != nullptr) { SimInput->ConsumeInputBundle(Bundle); }
}
