#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"

#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"
#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"
#include "V2/World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	// Delegate
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct IPhysicsModelDelegate {
		virtual ~IPhysicsModelDelegate() = default;

		virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
	};

	// Interface
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct FPhysicsModelBase : public IModelDriverDelegate {
		virtual ~FPhysicsModelBase() override = default;

		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) = 0;
		virtual void Cleanup() = 0;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) = 0;
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const = 0;
	};

	// Model declaration
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename InputPacketType>
	struct FPhysicsModel : public FPhysicsModelBase {
		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) override final;

		virtual ~FPhysicsModel() override = default;
		virtual void Cleanup() override final;

		virtual void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) override final;
		virtual void ReceiveInputPackets(FNetSerializationProxy& Proxy) const override final;

		// IModelDriverDelegate
		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Packets) override;
		virtual void ProduceInput(FInputPacketWrapper& Packet) override;

	public:

		DECLARE_DELEGATE_OneParam(FPhysicsModelProduceInput, InputPacketType&)
		FPhysicsModelProduceInput ProduceInputDelegate;

	private:
		class UPrimitiveComponent* CachedComponent = nullptr;
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};

	// Implementation
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::Initialize(UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate) {
		CachedComponent = Component;
		check(CachedComponent);

		Delegate = InDelegate;
		check(Delegate);

		const UWorld* World  = CachedComponent->GetWorld();
		check(World);

		CachedWorldManager = FWorldManager::ManagerForWorld(World);
		check(CachedWorldManager)
	}

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::Cleanup() {
		if (CachedWorldManager != nullptr && ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		CachedWorldManager = nullptr;
		ModelDriver = nullptr;
	}

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::SetNetRole(ENetRole Role, bool bShouldTakeInput,
		FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep) {
		check(CachedWorldManager);
		check(Delegate);

		if (ModelDriver != nullptr) {
			CachedWorldManager->RemoveTickCallback(ModelDriver.Get());
		}

		switch (Role) {
		case ROLE_Authority:
			// TODO pass bShouldTakeInput here
			ModelDriver = MakeUnique<FModelAuthDriver>(CachedComponent, this, AutoProxyRep, SimProxyRep);
			break;
		case ROLE_AutonomousProxy:
			ModelDriver = MakeUnique<FModelAutoProxyDriver>(CachedComponent, this, AutoProxyRep, CachedWorldManager->GetRewindBufferSize());
			break;
		case ROLE_SimulatedProxy:
			// TODO add in the sim proxy
			break;
		default:
			break;
		}

		if (ModelDriver != nullptr) {
			CachedWorldManager->AddTickCallback(ModelDriver.Get());
		}
	}

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::ReceiveInputPackets(FNetSerializationProxy& Proxy) const {
		if (ModelDriver == nullptr) { return; }

		TArray<FInputPacketWrapper> Packets;
		Proxy.NetSerializeFunc = [&](FArchive& Ar) {
			uint8 Size;
			Ar << Size;

			for (uint8 i = 0; i < Size; i++) {
				Packets.Emplace();
				FInputPacketWrapper& Packet = Packets.Last();
				Packet.Body = MakeShared<InputPacketType>();
				Packet.NetSerialize(Ar);
			}
		};

		Proxy.Deserialize();
		ModelDriver->ReceiveInputPackets(Packets);
	}

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::EmitInputPackets(TArray<FInputPacketWrapper>& Packets) {
		check(Delegate);

		FNetSerializationProxy Proxy;
		Proxy.NetSerializeFunc = [=](FArchive& Ar) mutable {
			uint8 Size = Packets.Num();
			Ar << Size;

			for (uint8 i = 0; i < Size; i++) {
				Packets[i].NetSerialize(Ar);
			}
		};


		Delegate->EmitInputPackets(Proxy);
	}

	template <typename InputPacketType>
	void FPhysicsModel<InputPacketType>::ProduceInput(FInputPacketWrapper& Packet) {
		TSharedPtr<InputPacketType> Body = MakeShared<InputPacketType>();
		ProduceInputDelegate.ExecuteIfBound(*Body.Get());

		Packet.Body = MoveTemp(Body);
	}
}
