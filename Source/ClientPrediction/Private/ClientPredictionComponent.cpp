#include "ClientPredictionComponent.h"
#include "Net/UnrealNetwork.h"

UClientPredictionComponent::UClientPredictionComponent() {
    SetIsReplicatedByDefault(true);
    bWantsInitializeComponent = true;

    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

UClientPredictionComponent::~UClientPredictionComponent() {
    DestroyModel();
}

void UClientPredictionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(UClientPredictionComponent, ControlRepProxy, COND_AutonomousOnly);
    DOREPLIFETIME(UClientPredictionComponent, FinalStateRepProxy);
}

void UClientPredictionComponent::InitializeComponent() {
    Super::InitializeComponent();

    UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
    check(UpdatedComponent);

    check(PhysicsModel);
    if (UpdatedComponent != nullptr && PhysicsModel != nullptr) {
        PhysicsModel->Initialize(UpdatedComponent, this, FinalStateRepProxy);
    }

    CheckOwnerRoleChanged();
}

void UClientPredictionComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) {
    Super::PreReplication(ChangedPropertyTracker);
    CheckOwnerRoleChanged();
}

void UClientPredictionComponent::PreNetReceive() {
    Super::PreNetReceive();
    CheckOwnerRoleChanged();
}

void UClientPredictionComponent::CheckOwnerRoleChanged() {
    if (PhysicsModel == nullptr) { return; }

    const AActor* OwnerActor = GetOwner();
    const ENetRole CurrentRole = OwnerActor->GetLocalRole();
    const bool bAuthorityTakesInput = OwnerActor->GetNetConnection() == nullptr;

    if (CachedRole == CurrentRole && bCachedAuthorityTakesInput == static_cast<uint8>(bAuthorityTakesInput)) { return; }

    CachedRole = CurrentRole;
    bCachedAuthorityTakesInput = bAuthorityTakesInput;

    PhysicsModel->SetNetRole(CurrentRole, bAuthorityTakesInput, ControlRepProxy);
}

void UClientPredictionComponent::BeginPlay() {
    Super::BeginPlay();
    CheckOwnerRoleChanged();
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    Super::EndPlay(EndPlayReason);
    DestroyModel();
}

void UClientPredictionComponent::UninitializeComponent() {
    Super::UninitializeComponent();
    DestroyModel();
}

void UClientPredictionComponent::DestroyModel() {
    if (PhysicsModel != nullptr) {
        PhysicsModel->Cleanup();
        PhysicsModel = nullptr;
    }
}

void UClientPredictionComponent::GetNetworkConditions(ClientPrediction::FNetworkConditions& NetworkConditions) const {
    NetworkConditions = {};

    const AActor* Owner = GetOwner();
    if (!Owner) { return; }

    const UNetConnection* NetConnection = Owner->GetNetConnection();
    if (NetConnection == nullptr) { return; }

    NetworkConditions.Latency = NetConnection->AvgLag;
    NetworkConditions.Jitter = NetConnection->GetAverageJitterInMS() / 1000.0;
}

void UClientPredictionComponent::RecvInputPacket_Implementation(FNetSerializationProxy Proxy) {
    if (PhysicsModel != nullptr) {
        PhysicsModel->ReceiveInputPackets(Proxy);
    }
}

void UClientPredictionComponent::EmitInputPackets(FNetSerializationProxy& Proxy) {
    if (PhysicsModel != nullptr) {
        RecvInputPacket(Proxy);
    }
}
