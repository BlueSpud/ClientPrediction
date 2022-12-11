#include "V2/ClientPredictionModelTypesV2.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {

	CLIENTPREDICTION_API float ClientPredictionPositionTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionPositionTolerance(TEXT("cp.PositionTolerance"), ClientPredictionPositionTolerance, TEXT("If the position deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionVelocityTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionVelocityTolerance(TEXT("cp.VelocityTolerance"), ClientPredictionVelocityTolerance, TEXT("If the velcoity deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionRotationTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionRotationTolerance(TEXT("cp.RotationTolerance"), ClientPredictionRotationTolerance, TEXT("If the rotation deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionAngularVelTolerance(TEXT("cp.AngularVelTolerance"), ClientPredictionAngularVelTolerance, TEXT("If the angular velocity deleta is less than this, a correction won't be applied"));

	void FPhysicsState::NetSerialize(FArchive& Ar) {
		Ar << TickNumber;
		Ar << InputPacketTickNumber;
		Ar << ObjectState;

		// Serialize manually to make sure that they are serialized as doubles
		Ar << X.X;
		Ar << X.Y;
		Ar << X.Z;

		Ar << V.X;
		Ar << V.Y;
		Ar << V.Z;

		Ar << R.X;
		Ar << R.Y;
		Ar << R.Z;
		Ar << R.W;

		Ar << W.X;
		Ar << W.Y;
		Ar << W.Z;
	}

	bool FPhysicsState::ShouldReconcile(const Chaos::FGeometryParticleState& State) const {
		if (State.ObjectState() != ObjectState) { return true; }
		if ((State.X() - X).Size() > ClientPredictionPositionTolerance) { return true; }
		if ((State.V() - V).Size() > ClientPredictionVelocityTolerance) { return true; }
		if ((State.R() - R).Size() > ClientPredictionRotationTolerance) { return true; }
		if ((State.W() - W).Size() > ClientPredictionAngularVelTolerance) { return true; }

		return false;
	}

	void FPhysicsState::FillState(const Chaos::FRigidBodyHandle_Internal* Handle) {
		X = Handle->X();
		V = Handle->V();
		R = Handle->R();
		W = Handle->W();
		ObjectState = Handle->ObjectState();
	}

	void FPhysicsState::Reconcile(Chaos::FRigidBodyHandle_Internal* Handle) const {
		Handle->SetX(X);
		Handle->SetV(V);
		Handle->SetR(R);
		Handle->SetW(W);
		Handle->SetObjectState(ObjectState);
	}
}
