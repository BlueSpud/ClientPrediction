#include "Driver/ClientPredictionRepProxy.h"

void FClientPredictionRepProxy::Dispatch() {
	if (!SerializeFunc) {
		UE_LOG(LogTemp, Error, TEXT("Dispatched with no function! %s"), *Name);
	}

	++SequenceNumber;
}

bool FClientPredictionRepProxy::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
	bOutSuccess = true;

	if (!SerializeFunc) {
		UE_LOG(LogTemp, Error, TEXT("Tried to replicate a rep proxy without a serialize function! %s"), *Name);
		return true;
	}

	SerializeFunc(Ar);
	return true;
}

bool FClientPredictionRepProxy::Identical(const FClientPredictionRepProxy* Other, uint32 PortFlags) const {
	return SequenceNumber == Other->SequenceNumber;
}
