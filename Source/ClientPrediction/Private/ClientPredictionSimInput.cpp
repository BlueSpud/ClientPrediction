#include "ClientPredictionSimInput.h"

#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"

bool FInputBundle::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
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
