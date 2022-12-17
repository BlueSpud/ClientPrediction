#pragma once

#include "ClientPredictionRepProxy.generated.h"

USTRUCT()
struct CLIENTPREDICTION_API FRepProxy {
    GENERATED_BODY()

    void Dispatch();

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FRepProxy* Other, uint32 PortFlags) const;

    TFunction<void(FArchive&)> SerializeFunc;
    FString Name;

private:
    uint32 SequenceNumber = 0;
};

template <>
struct TStructOpsTypeTraits<FRepProxy> : public TStructOpsTypeTraitsBase2<FRepProxy> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true,
    };
};
