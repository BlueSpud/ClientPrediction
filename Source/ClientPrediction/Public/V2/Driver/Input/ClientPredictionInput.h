#pragma once

namespace ClientPrediction {
    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;

        void NetSerialize(FArchive& Ar);
        friend FArchive& operator<<(FArchive& Ar, FInputPacketWrapper& Wrapper);

    };

    inline void FInputPacketWrapper::NetSerialize(FArchive& Ar) {
        Ar << PacketNumber;
    }

    inline FArchive& operator<<(FArchive& Ar, FInputPacketWrapper& Wrapper) {
        Wrapper.NetSerialize(Ar);
        return Ar;
    }
}