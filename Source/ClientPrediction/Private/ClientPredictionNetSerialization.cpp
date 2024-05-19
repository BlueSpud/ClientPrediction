#include "ClientPredictionNetSerialization.h"

bool FBundledPackets::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FBundledPackets::Identical(const FBundledPackets* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}

bool FBundledPacketsLow::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FBundledPacketsLow::Identical(const FBundledPacketsLow* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}

bool FBundledPacketsFull::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FBundledPacketsFull::Identical(const FBundledPacketsFull* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}
