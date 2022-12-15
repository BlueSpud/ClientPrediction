#pragma once
#include "V2/Driver/ClientPredictionModelDriverV2.h"

namespace ClientPrediction {
	class FSimulatedModelDriver : public IModelDriver {

	public:
		FSimulatedModelDriver(UPrimitiveComponent* UpdatedComponent, IModelDriverDelegate* Delegate);

	protected:
		class Chaos::FRigidBodyHandle_Internal* GetPhysicsHandle() const;

		void PreTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt);
		void PostTickSimulateWithCurrentInput(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal Time);

	protected:
		UPrimitiveComponent* UpdatedComponent = nullptr;
		IModelDriverDelegate* Delegate = nullptr;

		FInputPacketWrapper CurrentInput{}; // Only used on physics thread
		FPhysicsState CurrentState{}; // Only used on physics thread
		FPhysicsState LastState{}; // Only used on physics thread
	};
}
