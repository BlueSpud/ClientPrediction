#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

#include "../ClientPredictionModel.h"
#include "ClientPredictionPhysicsDeclares.h"
#include "ClientPredictionPhysicsContext.h"

struct CLIENTPREDICTION_API FEmptyState {
	void NetSerialize(FArchive& Ar) {}
	void Rewind(class UPrimitiveComponent* Component) const {}
	void Interpolate(float Alpha, const FEmptyState& Other) {}
	bool operator ==(const FEmptyState& Other) const { return true; }
};

template <typename InputPacket, typename ModelState>
class BaseClientPredictionPhysicsModel : public BaseClientPredictionModel<InputPacket, FPhysicsStateWrapper<ModelState>> {
	
using WrappedModelState = FPhysicsStateWrapper<ModelState>;

public:

	BaseClientPredictionPhysicsModel();
	virtual ~BaseClientPredictionPhysicsModel() override;

	virtual void Initialize(UPrimitiveComponent* Component, ENetRole Role) override final;
	virtual void Initialize(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, ENetRole Role);

protected:
	
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const WrappedModelState& PrevState, WrappedModelState& OutState, const InputPacket& Input) override final;
	virtual void Rewind(const WrappedModelState& State, UPrimitiveComponent* Component) override final;

	// Should be implemented by child classes
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const FPhysicsContext& Context, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input);

	virtual void ApplyState(UPrimitiveComponent* Component, const WrappedModelState& State) override;

private:

	void UpdateWorld(UPrimitiveComponent* Component);
	
private:
	
	ImmediatePhysics::FActorHandle* SimulatedBodyHandle = nullptr;
	ImmediatePhysics::FSimulation* PhysicsSimulation = nullptr;

	struct FSimulationActor {

		FSimulationActor(ImmediatePhysics::FActorHandle* Handle) : Handle(Handle) {}

		/** Memory is managed by simulation */
		ImmediatePhysics::FActorHandle* Handle = nullptr;

		/** The number of ticks ago that this actor was last seen by an overlap cast */
		uint32 TicksSinceLastSeen = 0;
		
	};

	TMap<const UPrimitiveComponent*, FSimulationActor> StaticSimulationActors;
	
};

template <typename InputPacket, typename ModelState>
BaseClientPredictionPhysicsModel<InputPacket, ModelState>::BaseClientPredictionPhysicsModel() {
	PhysicsSimulation = new ImmediatePhysics::FSimulation();
}

template <typename InputPacket, typename ModelState>
BaseClientPredictionPhysicsModel<InputPacket, ModelState>::~BaseClientPredictionPhysicsModel() {
	// SimulatedBodyHandle is managed by the physics simulation
	delete PhysicsSimulation;
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Initialize(UPrimitiveComponent* Component, ENetRole Role) {
	SimulatedBodyHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::DynamicActor, Component->GetBodyInstance(), Component->GetComponentTransform());
	check(SimulatedBodyHandle);
	SimulatedBodyHandle->SetEnabled(true);
	PhysicsSimulation->SetNumActiveBodies(1, {0});
	
	Initialize(Component, SimulatedBodyHandle, Role);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Initialize(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle, ENetRole Role) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const WrappedModelState& PrevState, WrappedModelState& OutState, const InputPacket& Input) {
	UpdateWorld(Component);

	FPhysicsContext Context(SimulatedBodyHandle, Component);
	Simulate(Dt, Component, Context, PrevState.State, OutState.State, Input);

	PhysicsSimulation->SetSolverSettings(Dt, -1.0, -1.0f, 5, 5, 5);
	PhysicsSimulation->Simulate(Dt, 1.0, 1, FVector(0.0, 0.0, -980.0));

	const FTransform WorldTransform = SimulatedBodyHandle->GetWorldTransform();
	
	OutState.PhysicsState.Location = WorldTransform.GetLocation();
	OutState.PhysicsState.Rotation = WorldTransform.GetRotation();
	OutState.PhysicsState.LinearVelocity = SimulatedBodyHandle->GetLinearVelocity();
	OutState.PhysicsState.AngularVelocity = SimulatedBodyHandle->GetAngularVelocity();
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Rewind(const WrappedModelState& State, UPrimitiveComponent* Component) {
	State.Rewind(Component, SimulatedBodyHandle);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const FPhysicsContext& Context, const ModelState& PrevState, ModelState& OutState, const InputPacket& Input) {}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::ApplyState(UPrimitiveComponent* Component, const WrappedModelState& State) {
	Component->SetWorldLocation(State.PhysicsState.Location);
	Component->SetWorldRotation(State.PhysicsState.Rotation);
}

template <typename InputPacket, typename ModelState>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState>::UpdateWorld(UPrimitiveComponent* Component) {
	UWorld* UnsafeWorld = Component->GetWorld();
	FPhysScene* PhysScene = UnsafeWorld->GetPhysicsScene();
	
	if ((UnsafeWorld != nullptr) && (PhysScene != nullptr))
	{
		TArray<FOverlapResult> Overlaps;
		AActor* Owner = Cast<AActor>(Component->GetOwner());

		// For now, only support static objects for simplicity
		UnsafeWorld->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams::AllStaticObjects, FCollisionShape::MakeSphere(10000.0));

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent())
			{
				if (StaticSimulationActors.Find(OverlapComp) != nullptr) {
					StaticSimulationActors[OverlapComp].TicksSinceLastSeen = 0;
					continue;
				}
				
				const bool bIsSelf = (Owner == OverlapComp->GetOwner());
				if (!bIsSelf)
				{
					// Create a kinematic actor. Not using Static as world-static objects may move in the simulation's frame of reference
					ImmediatePhysics::FActorHandle* ActorHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::KinematicActor, &OverlapComp->BodyInstance, OverlapComp->GetComponentTransform());
					PhysicsSimulation->AddToCollidingPairs(ActorHandle);

					StaticSimulationActors.Add(OverlapComp, FSimulationActor(ActorHandle));
				}
			}
		}
	}
}
