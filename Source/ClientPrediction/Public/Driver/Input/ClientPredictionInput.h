#pragma once

namespace ClientPrediction {
    template <typename InputType>
    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;
        InputType Body{};

        void NetSerialize(FArchive& Ar);

        template <typename InputPacket_>
        friend FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket_>& Wrapper);
    };

    template <typename InputType>
    void FInputPacketWrapper<InputType>::NetSerialize(FArchive& Ar) {
        Ar << PacketNumber;
        Body.NetSerialize(Ar);
    }

    template <typename InputPacket>
    FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket>& Wrapper) {
        Wrapper.NetSerialize(Ar);
        return Ar;
    }
}
