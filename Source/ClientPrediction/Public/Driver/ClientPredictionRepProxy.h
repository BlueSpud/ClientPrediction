#pragma once

#include "ClientPredictionRepProxy.generated.h"

USTRUCT()
struct CLIENTPREDICTION_API FClientPredictionRepProxy {

	GENERATED_BODY()

	void Dispatch();

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	bool Identical(const FClientPredictionRepProxy* Other, uint32 PortFlags) const;

	TFunction<void(FArchive&)> SerializeFunc;
	FString Name;

private:

	uint32 SequenceNumber = 0;

};

template<>
struct TStructOpsTypeTraits<FClientPredictionRepProxy> : public TStructOpsTypeTraitsBase2<FClientPredictionRepProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
};