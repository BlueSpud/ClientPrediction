#pragma once

#include "CoreMinimal.h"

#include "Driver/ClientPredictionRepProxy.h"
#include "V2/Driver/ClientPredictionModelDriverV2.h"

#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"
#include "V2/Driver/Drivers/ClientPredictionModelAutoProxyDriverV2.h"
#include "V2/World/ClientPredictionWorldManager.h"

namespace ClientPrediction {

	struct IPhysicsModelDelegate {
		virtual ~IPhysicsModelDelegate() = default;

		virtual void EmitInputPackets(FNetSerializationProxy& Proxy) = 0;
	};

	template <typename InputPacketType>
	struct FPhysicsModel : public IModelDriverDelegate {
		virtual void Initialize(class UPrimitiveComponent* Component, IPhysicsModelDelegate* InDelegate);

		virtual ~FPhysicsModel() = default;
		void Cleanup();

		void SetNetRole(ENetRole Role, bool bShouldTakeInput, FClientPredictionRepProxy& AutoProxyRep, FClientPredictionRepProxy& SimProxyRep);
		void ReceiveInputPackets(FNetSerializationProxy& Proxy) const;

		// IModelDriverDelegate
		virtual void EmitInputPackets(TArray<FInputPacketWrapper>& Packets) override;
		virtual void ProduceInput(FInputPacketWrapper& Packet) override;

	private:
		class UPrimitiveComponent* CachedComponent = nullptr;
		struct FWorldManager* CachedWorldManager = nullptr;
		TUniquePtr<IModelDriver> ModelDriver = nullptr;
		IPhysicsModelDelegate* Delegate = nullptr;
	};

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
		Packet.Body = MakeShared<InputPacketType>();

		// TODO Produce input
	}
}
