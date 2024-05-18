#include "ClientPredictionNetSerialization.h"

bool FBundledPackets::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FBundledPackets::Identical(const FBundledPackets* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}

bool FSimProxyBundledPackets::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FSimProxyBundledPackets::Identical(const FSimProxyBundledPackets* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}

bool FAutoProxyBundledPackets::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    return Impl.NetSerialize(Ar, Map, bOutSuccess);
}

bool FAutoProxyBundledPackets::Identical(const FAutoProxyBundledPackets* Other, uint32 PortFlags) const {
    return Impl.Identical(&Other->Impl, PortFlags);
}
