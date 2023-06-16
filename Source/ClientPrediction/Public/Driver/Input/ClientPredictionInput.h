#pragma once

namespace ClientPrediction {
    template <typename InputType>
    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;
        InputType Body{};

        /**
         * This contains the estimated time elapsed in seconds since the auto proxy generated this input packet. This is only calculated on the authority. */
        Chaos::FReal EstimatedDelayFromClient = 0.0;

        /**
         * This contains the estimated time between when this state was generated and the point in time that the auto proxy was seeing for the simulated proxies.
         * This value is useful for hit registration since going back in time by this amount will show the world as the auto proxy saw it when it was generating input.
         * This is only calculated on the authority.
         */
        Chaos::FReal EstimatedClientSimProxyDelay = 0.0;

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
