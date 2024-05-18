#pragma once
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"

#include "ClientPredictionNetSerialization.generated.h"

/**
 * In order to send different kind of input packets / states, we need to be able to have one RPC
 * handle several structs. In order to do this, we use a proxy to call a serialization function
 * of some unknown struct.
 */
USTRUCT()
struct FNetSerializationProxy {
    GENERATED_BODY()

    FNetSerializationProxy() = default;

    FNetSerializationProxy(TFunction<void(FArchive& Ar)> NetSerializeFunc) {
        this->NetSerializeFunc = NetSerializeFunc;
    }

    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
        if (Ar.IsLoading()) {
            PackageMap = Map;

            TArray<uint8> CompressedBuffer;
            Ar << NumberOfBits;
            Ar << CompressedBuffer;

            FArchiveLoadCompressedProxy Decompressor(CompressedBuffer, NAME_Zlib);
            Decompressor << SerializedBits;
        }
        else {
            checkSlow(NetSerializeFunc);

            FNetBitWriter Writer(nullptr, 32768);
            NetSerializeFunc(Writer);

            TArray<uint8> UncompressedBuffer = *Writer.GetBuffer();

            TArray<uint8> CompressedBuffer;
            FArchiveSaveCompressedProxy Compressor(CompressedBuffer, NAME_Zlib);
            Compressor << UncompressedBuffer;
            Compressor.Flush();

            int64 NumBits = Writer.GetNumBits();
            Ar << NumBits;
            Ar << CompressedBuffer;
        }

        bOutSuccess = true;
        return true;
    }

    /** To be called after receiving the proxy from an RPC. This will deserialize the data. */
    bool Deserialize() {
        if (NumberOfBits == -1 || PackageMap == nullptr) {
            return false;
        }

        FNetBitReader BitReader(PackageMap, SerializedBits.GetData(), NumberOfBits);
        NetSerializeFunc(BitReader);

        NumberOfBits = -1;
        PackageMap = nullptr;
        return true;
    }

    TFunction<void(FArchive& Ar)> NetSerializeFunc;

    UPackageMap* PackageMap = nullptr;
    TArray<uint8> SerializedBits;
    int64 NumberOfBits = -1;
};

template <>
struct TStructOpsTypeTraits<FNetSerializationProxy> : public TStructOpsTypeTraitsBase2<FNetSerializationProxy> {
    enum {
        WithNetSerializer = true
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <ENetRole Role>
struct FPacketBundle {
    template <typename Packet>
    void Store(TArray<Packet>& Packets);

    template <typename Packet>
    bool Retrieve(TArray<Packet>& Packets) const;

private:
    template <typename Packet>
    void NetSerializePacket(Packet& PacketToSerialize, FArchive& Ar) const;

public:
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FPacketBundle* Other, uint32 PortFlags) const;

private:
    TArray<uint8> SerializedBits;
    int32 NumberOfBits = -1;
    int32 Sequence = 0;
};

template <ENetRole Role>
template <typename Packet>
void FPacketBundle<Role>::Store(TArray<Packet>& Packets) {
    check(Packets.Num() < TNumericLimits<uint8>::Max());

    FNetBitWriter Writer(nullptr, TNumericLimits<uint16>::Max());
    uint8 NumPackets = static_cast<uint8>(Packets.Num());
    Writer << NumPackets;

    for (Packet& PacketToWrite : Packets) {
        NetSerializePacket(PacketToWrite, Writer);
    }

    SerializedBits = *Writer.GetBuffer();
    NumberOfBits = Writer.GetNumBits();
    ++Sequence;
}

template <ENetRole Role>
template <typename Packet>
bool FPacketBundle<Role>::Retrieve(TArray<Packet>& Packets) const {
    if (NumberOfBits == -1) { return false; }

    FNetBitReader BitReader(nullptr, SerializedBits.GetData(), NumberOfBits);
    uint8 NumPackets = 0;
    BitReader << NumPackets;

    for (uint8 PacketIdx = 0; PacketIdx < NumPackets; ++PacketIdx) {
        Packets.AddDefaulted();
        NetSerializePacket(Packets.Last(), BitReader);
    }

    return true;
}

template <ENetRole Role>
template <typename Packet>
void FPacketBundle<Role>::NetSerializePacket(Packet& PacketToSerialize, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar);
}

template <>
template <typename Packet>
void FPacketBundle<ENetRole::ROLE_AutonomousProxy>::NetSerializePacket(Packet& PacketToSerialize, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar, ENetRole::ROLE_AutonomousProxy);
}

template <>
template <typename Packet>
void FPacketBundle<ENetRole::ROLE_SimulatedProxy>::NetSerializePacket(Packet& PacketToSerialize, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar, ENetRole::ROLE_SimulatedProxy);
}

template <ENetRole Role>
bool FPacketBundle<Role>::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    if (Ar.IsLoading()) {
        TArray<uint8> CompressedBuffer;
        Ar << NumberOfBits;
        Ar << CompressedBuffer;

        FArchiveLoadCompressedProxy Decompressor(CompressedBuffer, NAME_Zlib);
        Decompressor << SerializedBits;
    }
    else {
        TArray<uint8> CompressedBuffer;
        FArchiveSaveCompressedProxy Compressor(CompressedBuffer, NAME_Zlib);
        Compressor << SerializedBits;
        Compressor.Flush();

        Ar << NumberOfBits;
        Ar << CompressedBuffer;
    }

    bOutSuccess = true;
    return true;
}

template <ENetRole Role>
bool FPacketBundle<Role>::Identical(const FPacketBundle* Other, uint32 PortFlags) const {
    return Sequence == Other->Sequence;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FBundledPackets {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ENetRole::ROLE_None>;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FBundledPackets* Other, uint32 PortFlags) const;

    BundleType& Bundle() const { return Impl; }

private:
    mutable BundleType Impl;
};

template <>
struct TStructOpsTypeTraits<FBundledPackets> : public TStructOpsTypeTraitsBase2<FBundledPackets> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};

USTRUCT()
struct FSimProxyBundledPackets {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ENetRole::ROLE_SimulatedProxy>;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FSimProxyBundledPackets* Other, uint32 PortFlags) const;

    BundleType& Bundle() const { return Impl; }

private:
    mutable BundleType Impl;
};

template <>
struct TStructOpsTypeTraits<FSimProxyBundledPackets> : public TStructOpsTypeTraitsBase2<FSimProxyBundledPackets> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};

USTRUCT()
struct FAutoProxyBundledPackets {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ENetRole::ROLE_AutonomousProxy>;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FAutoProxyBundledPackets* Other, uint32 PortFlags) const;

    BundleType& Bundle() const { return Impl; }

private:
    mutable BundleType Impl;
};

template <>
struct TStructOpsTypeTraits<FAutoProxyBundledPackets> : public TStructOpsTypeTraitsBase2<FAutoProxyBundledPackets> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};
