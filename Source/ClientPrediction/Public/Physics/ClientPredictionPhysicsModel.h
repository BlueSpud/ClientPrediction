#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

#include "../ClientPredictionModel.h"
#include "ClientPredictionPhysicsDeclares.h"
#include "ClientPredictionPhysicsContext.h"

/** The number of ticks an actor is allowed to not have been seen in the overlap before being removed from the simulation. */
static constexpr uint32 kActorLifetime = 30;

/** The radius of the sphere used to find and add components into the simulation */
static constexpr float kSimulationQueryRange = 10000.0;

struct CLIENTPREDICTION_API FEmptyState {
	void NetSerialize(FArchive& Ar) {}
	void Rewind(class UPrimitiveComponent* Component) const {}
	void Interpolate(float Alpha, const FEmptyState& Other) {}
	bool operator ==(const FEmptyState& Other) const { return true; }
};

template <typename InputPacket, typename ModelState, typename CueSet>
class BaseClientPredictionPhysicsModel : public BaseClientPredictionModel<InputPacket, FPhysicsStateWrapper<ModelState>, CueSet> {
	using WrappedModelState = FPhysicsStateWrapper<ModelState>;
	using SimOutput = FSimulationOutput<WrappedModelState, CueSet>;
	using PhysicsSimOutput = FPhysicsSimulationOutput<ModelState, CueSet>;

public:

	BaseClientPredictionPhysicsModel();
	virtual ~BaseClientPredictionPhysicsModel() override;

protected:
	virtual void Initialize(UPrimitiveComponent* Component) override final;
	virtual void InitializeModel(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle);

	virtual void GenerateInitialState(FPhysicsStateWrapper<ModelState>& State) override final;
	virtual void GenerateInitialModelState(ModelState& State);

	virtual void BeginTick(Chaos::FReal Dt, FPhysicsStateWrapper<ModelState>& State, UPrimitiveComponent* Component) override;
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const WrappedModelState& PrevState, SimOutput& Output, const InputPacket& Input) override final;
	virtual void Rewind(const WrappedModelState& State, UPrimitiveComponent* Component) override final;

	// Should be implemented by child classes
	virtual void Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const FPhysicsContext& Context, const ModelState& PrevState, PhysicsSimOutput& Output, const InputPacket& Input);

	virtual void ApplyState(UPrimitiveComponent* Component, const WrappedModelState& State) override;

private:

	void FillPhysicsState(FPhysicsStateWrapper<ModelState>& State);
	void UpdateWorld(UPrimitiveComponent* Component);
	void RemoveOldActors();

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

template <typename InputPacket, typename ModelState, typename CueSet>
BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::BaseClientPredictionPhysicsModel() {
	PhysicsSimulation = new ImmediatePhysics::FSimulation();
}

template <typename InputPacket, typename ModelState, typename CueSet>
BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::~BaseClientPredictionPhysicsModel() {
	// SimulatedBodyHandle is managed by the physics simulation
	delete PhysicsSimulation;
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::Initialize(UPrimitiveComponent* Component) {
	SimulatedBodyHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::DynamicActor, Component->GetBodyInstance(), Component->GetComponentTransform());
	check(SimulatedBodyHandle);
	SimulatedBodyHandle->SetEnabled(true);
	PhysicsSimulation->SetNumActiveBodies(1, {0});

	InitializeModel(Component, SimulatedBodyHandle);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::InitializeModel(UPrimitiveComponent* Component, ImmediatePhysics::FActorHandle* Handle) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::GenerateInitialState(FPhysicsStateWrapper<ModelState>& State) {
	FillPhysicsState(State);
	GenerateInitialModelState(State.State);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::GenerateInitialModelState(ModelState& State) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::BeginTick(Chaos::FReal Dt, FPhysicsStateWrapper<ModelState>& State, UPrimitiveComponent* Component) {
	UpdateWorld(Component);

	PhysicsSimulation->SetSolverSettings(Dt, -1.0, -1.0f, 5, 5, 5);
	PhysicsSimulation->Simulate(Dt, 1.0, 1, FVector(0.0, 0.0, -980.0));

	FillPhysicsState(State);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const WrappedModelState& PrevState, SimOutput& Output, const InputPacket& Input) {
	FPhysicsContext Context(SimulatedBodyHandle, Component, FTransform(PrevState.PhysicsState.Rotation, PrevState.PhysicsState.Location));
	FPhysicsSimulationOutput<ModelState, CueSet> PhysicsOutput(Output);

	Simulate(Dt, Component, Context, PrevState.State, PhysicsOutput, Input);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::Rewind(const WrappedModelState& State, UPrimitiveComponent* Component) {
	State.Rewind(Component, SimulatedBodyHandle);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::Simulate(Chaos::FReal Dt, UPrimitiveComponent* Component, const FPhysicsContext& Context, const ModelState& PrevState, PhysicsSimOutput& Output, const InputPacket& Input) {}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::ApplyState(UPrimitiveComponent* Component, const WrappedModelState& State) {
	Component->SetWorldLocation(State.PhysicsState.Location);
	Component->SetWorldRotation(State.PhysicsState.Rotation);
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::FillPhysicsState(FPhysicsStateWrapper<ModelState>& State) {
	const FTransform WorldTransform = SimulatedBodyHandle->GetWorldTransform();

	State.PhysicsState.Location = WorldTransform.GetLocation();
	State.PhysicsState.Rotation = WorldTransform.GetRotation();
	State.PhysicsState.LinearVelocity = SimulatedBodyHandle->GetLinearVelocity();
	State.PhysicsState.AngularVelocity = SimulatedBodyHandle->GetAngularVelocity();
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::UpdateWorld(UPrimitiveComponent* Component) {
	const UWorld* UnsafeWorld = Component->GetWorld();
	const FPhysScene* PhysScene = UnsafeWorld->GetPhysicsScene();

	for (auto& Entry : StaticSimulationActors) {
		++Entry.Value.TicksSinceLastSeen;
	}

	if ((UnsafeWorld != nullptr) && (PhysScene != nullptr))
	{
		TArray<FOverlapResult> Overlaps;
		const AActor* Owner = Cast<AActor>(Component->GetOwner());

		// For now, only support static objects for simplicity
		UnsafeWorld->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams::AllStaticObjects, FCollisionShape::MakeSphere(kSimulationQueryRange));

		for (const FOverlapResult& Overlap : Overlaps) {
			if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent()) {
				if (StaticSimulationActors.Find(OverlapComp) != nullptr) {
					StaticSimulationActors[OverlapComp].TicksSinceLastSeen = 0;
					continue;
				}

				const bool bIsSelf = (Owner == OverlapComp->GetOwner());
				if (!bIsSelf) {
					ImmediatePhysics::FActorHandle* ActorHandle = PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::StaticActor, &OverlapComp->BodyInstance, OverlapComp->GetComponentTransform());
					PhysicsSimulation->AddToCollidingPairs(ActorHandle);

					StaticSimulationActors.Add(OverlapComp, FSimulationActor(ActorHandle));
				}
			}
		}
	}

	RemoveOldActors();
}

template <typename InputPacket, typename ModelState, typename CueSet>
void BaseClientPredictionPhysicsModel<InputPacket, ModelState, CueSet>::RemoveOldActors() {
	TArray<const UPrimitiveComponent*> RemovedComponents;
	for (const auto& Entry : StaticSimulationActors) {
		const FSimulationActor& Actor = Entry.Value;

		if (Actor.TicksSinceLastSeen >= kActorLifetime) {
			PhysicsSimulation->DestroyActor(Actor.Handle);
			RemovedComponents.Add(Entry.Key);
		}
	}

	// Remove all the unused actors in a second loop so that the map is not being modified as it is being
	// iterated through.
	for (const UPrimitiveComponent* PurgedEntry : RemovedComponents) {
		StaticSimulationActors.Remove(PurgedEntry);
	}
}
