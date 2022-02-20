#pragma once

#include "ClientPredictionNetSerialization.generated.h"

/**
 * In order to send different kind of input packets / states, we need to be able to have one RPC
 * handle several structs. In order to do this, we use a proxy to call a serialization function
 * of some unknown struct.
 */ 
USTRUCT()
struct FNetSerializationProxy {

	GENERATED_BODY()

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
		if (Ar.IsLoading()) {
			FNetBitReader& BitReader = static_cast<FNetBitReader&>(Ar);
			NumberOfBits = BitReader.GetBitsLeft();
			PackageMap = Map;

			const int64 BytesLeft = BitReader.GetBytesLeft();
			SerializedBits.Reset(BytesLeft);
			SerializedBits.SetNumUninitialized(BytesLeft);
			SerializedBits.Last() = 0;

			BitReader.SerializeBits(SerializedBits.GetData(), NumberOfBits);
		} else {
			checkSlow(NetSerializeFunc());
			NetSerializeFunc(Ar);
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