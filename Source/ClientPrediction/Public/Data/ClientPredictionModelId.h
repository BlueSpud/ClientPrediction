#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionModelId.generated.h"

USTRUCT()
struct CLIENTPREDICTION_API FClientPredictionModelId {
	GENERATED_BODY()

	FClientPredictionModelId() : OwningActor(nullptr) {}
	FClientPredictionModelId(const FClientPredictionModelId& ModelId) : OwningActor(ModelId.OwningActor) { }
	explicit FClientPredictionModelId(AActor* OwningActor) : OwningActor(OwningActor) { }

	void NetSerialize(FArchive& Ar, class UPackageMap* Map) {
		Map->SerializeObject(Ar, AActor::StaticClass(), OwningActor);
	}

	bool operator==(const FClientPredictionModelId& Other) const {
		return Equals(Other);
	}

	bool Equals(const FClientPredictionModelId& Other) const {
		return OwningActor == Other.OwningActor;
	}

	uint32 GetTypeHash() const {
		return ::GetTypeHash(OwningActor);
	}

	/** Maps the ID to the player that owns the simulation. This is only valid on the authority */
	UPlayer* MapToOwningPlayer() const {
		AActor* Actor = Cast<AActor>(OwningActor);
		return Actor != nullptr ? Actor->GetNetOwningPlayer() : nullptr;
	}

private:
	UPROPERTY(Transient)
	UObject* OwningActor = nullptr;
};

FORCEINLINE uint32 GetTypeHash(const FClientPredictionModelId& ModelId) {
	return GetTypeHash(ModelId.GetTypeHash());
}
