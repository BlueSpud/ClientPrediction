#pragma once

namespace ClientPrediction {
    template <typename InputType>
    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;
        InputType Body{};

        /**
         * This contains an estimation of the server tick that was currently being displayed on the remote that sampled this input. 
         */
        float EstimatedDisplayedServerTick = INDEX_NONE;

        void NetSerialize(FArchive& Ar);

        template <typename InputPacket_>
        friend FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket_>& Wrapper);
    };

    template <typename InputType>
    void FInputPacketWrapper<InputType>::NetSerialize(FArchive& Ar) {
        Ar << PacketNumber;
        Ar << EstimatedDisplayedServerTick;
        Body.NetSerialize(Ar);
    }

    template <typename InputPacket>
    FArchive& operator<<(FArchive& Ar, FInputPacketWrapper<InputPacket>& Wrapper) {
        Wrapper.NetSerialize(Ar);
        return Ar;
    }
}
