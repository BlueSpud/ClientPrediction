#pragma once

#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"

#include "ClientPredictionDataCompleteness.h"

#include "ClientPredictionNetSerialization.generated.h"

template <ClientPrediction::EDataCompleteness Completeness>
struct FPacketBundle {
    void Copy(const FPacketBundle& Other);

    template <typename Packet, typename UserdataType>
    void Store(TArray<Packet>& Packets, UserdataType Userdata);

    template <typename Packet, typename UserdataType>
    bool Retrieve(TArray<Packet>& Packets, UserdataType Userdata) const;

private:
    template <typename Packet, typename UserdataType>
    void NetSerializePacket(Packet& PacketToSerialize, UserdataType Userdata, FArchive& Ar) const;

public:
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FPacketBundle* Other, uint32 PortFlags) const;

private:
    TArray<uint8> SerializedBits;
    int32 NumberOfBits = -1;
    int32 Sequence = 0;
};

template <ClientPrediction::EDataCompleteness Completeness>
void FPacketBundle<Completeness>::Copy(const FPacketBundle& Other) {
    SerializedBits = Other.SerializedBits;
    NumberOfBits = Other.NumberOfBits;

    Sequence = FMath::Max(Other.Sequence, ++Sequence);
}

template <ClientPrediction::EDataCompleteness Completeness>
template <typename Packet, typename UserdataType>
void FPacketBundle<Completeness>::Store(TArray<Packet>& Packets, UserdataType Userdata) {
    check(Packets.Num() < TNumericLimits<uint8>::Max());

    FNetBitWriter Writer(nullptr, TNumericLimits<uint16>::Max());
    uint8 NumPackets = static_cast<uint8>(Packets.Num());
    Writer << NumPackets;

    for (Packet& PacketToWrite : Packets) {
        NetSerializePacket(PacketToWrite, Userdata, Writer);
    }

    SerializedBits = *Writer.GetBuffer();
    NumberOfBits = Writer.GetNumBits();
    ++Sequence;
}

template <ClientPrediction::EDataCompleteness Completeness>
template <typename Packet, typename UserdataType>
bool FPacketBundle<Completeness>::Retrieve(TArray<Packet>& Packets, UserdataType Userdata) const {
    if (NumberOfBits == -1) { return false; }

    FNetBitReader BitReader(nullptr, SerializedBits.GetData(), NumberOfBits);
    uint8 NumPackets = 0;
    BitReader << NumPackets;

    for (uint8 PacketIdx = 0; PacketIdx < NumPackets; ++PacketIdx) {
        Packets.AddDefaulted();
        NetSerializePacket(Packets.Last(), Userdata, BitReader);
    }

    return true;
}

template <ClientPrediction::EDataCompleteness Completeness>
template <typename Packet, typename UserdataType>
void FPacketBundle<Completeness>::NetSerializePacket(Packet& PacketToSerialize, UserdataType Userdata, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar, Userdata);
}

template <>
template <typename Packet, typename UserdataType>
void FPacketBundle<ClientPrediction::EDataCompleteness::kFull>::NetSerializePacket(Packet& PacketToSerialize, UserdataType Userdata, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar, ClientPrediction::EDataCompleteness::kFull, Userdata);
}

template <>
template <typename Packet, typename UserdataType>
void FPacketBundle<ClientPrediction::EDataCompleteness::kLow>::NetSerializePacket(Packet& PacketToSerialize, UserdataType Userdata, FArchive& Ar) const {
    PacketToSerialize.NetSerialize(Ar, ClientPrediction::EDataCompleteness::kLow, Userdata);
}

template <ClientPrediction::EDataCompleteness Completeness>
bool FPacketBundle<Completeness>::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
    if (Ar.IsLoading()) {
        TArray<uint8> CompressedBuffer;
        Ar << NumberOfBits;
        Ar << CompressedBuffer;

        FArchiveLoadCompressedProxy Decompressor(CompressedBuffer, NAME_Zlib);
        Decompressor << SerializedBits;

        ++Sequence;
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

template <ClientPrediction::EDataCompleteness Completeness>
bool FPacketBundle<Completeness>::Identical(const FPacketBundle* Other, uint32 PortFlags) const {
    return Sequence == Other->Sequence;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FBundledPackets {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ClientPrediction::EDataCompleteness::kCount>;

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
struct FBundledPacketsLow {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ClientPrediction::EDataCompleteness::kLow>;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FBundledPacketsLow* Other, uint32 PortFlags) const;

    BundleType& Bundle() const { return Impl; }

private:
    mutable BundleType Impl;
};

template <>
struct TStructOpsTypeTraits<FBundledPacketsLow> : public TStructOpsTypeTraitsBase2<FBundledPacketsLow> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};

USTRUCT()
struct FBundledPacketsFull {
    GENERATED_BODY()

    using BundleType = FPacketBundle<ClientPrediction::EDataCompleteness::kFull>;

    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    bool Identical(const FBundledPacketsFull* Other, uint32 PortFlags) const;

    BundleType& Bundle() const { return Impl; }

private:
    mutable BundleType Impl;
};

template <>
struct TStructOpsTypeTraits<FBundledPacketsFull> : public TStructOpsTypeTraitsBase2<FBundledPacketsFull> {
    enum {
        WithNetSerializer = true,
        WithIdentical = true
    };
};
