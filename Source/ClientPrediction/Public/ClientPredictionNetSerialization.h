#pragma once
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/BufferArchive.h"

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
		} else {
			checkSlow(NetSerializeFunc());

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

template<>
struct TStructOpsTypeTraits<FNetSerializationProxy> : public TStructOpsTypeTraitsBase2<FNetSerializationProxy>
{
	enum
	{
		WithNetSerializer = true
	};
};