#include "ClientPredictionComponent.h"
#include "Net/UnrealNetwork.h"

UClientPredictionComponent::UClientPredictionComponent() {
    SetIsReplicatedByDefault(true);
    bWantsInitializeComponent = true;

    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
}

void UClientPredictionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(UClientPredictionComponent, AutoProxyRep, COND_AutonomousOnly);
    DOREPLIFETIME_CONDITION(UClientPredictionComponent, SimProxyRep, COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UClientPredictionComponent, ControlProxyRep, COND_AutonomousOnly);
}

void UClientPredictionComponent::InitializeComponent() {
    Super::InitializeComponent();

    UpdatedComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
    check(UpdatedComponent);

    check(PhysicsModel);
    PhysicsModel->Initialize(UpdatedComponent, this);

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

    PhysicsModel->SetNetRole(CurrentRole, bAuthorityTakesInput, AutoProxyRep, SimProxyRep, ControlProxyRep);
}

void UClientPredictionComponent::BeginPlay() {
    Super::BeginPlay();
    CheckOwnerRoleChanged();
}

void UClientPredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    Super::EndPlay(EndPlayReason);
    if (PhysicsModel != nullptr) {
        PhysicsModel->Cleanup();
    }
}

void UClientPredictionComponent::EmitReliableAuthorityState(FNetSerializationProxy& Proxy) {
    RecvReliableAuthorityState(Proxy);
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

void UClientPredictionComponent::RecvReliableAuthorityState_Implementation(FNetSerializationProxy Proxy) {
    if (PhysicsModel != nullptr) {
        PhysicsModel->ReceiveReliableAuthorityState(Proxy);
    }
}

