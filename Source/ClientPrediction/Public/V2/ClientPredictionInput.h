#pragma once

namespace ClientPrediction {
    struct CLIENTPREDICTION_API IInputPacket {
        virtual ~IInputPacket() = default;
        virtual void NetSerialize(FArchive& Ar) = 0;
    };

    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;
        TSharedPtr<IInputPacket> Body = nullptr;

        void NetSerialize(FArchive& Ar);
    };

    inline void FInputPacketWrapper::NetSerialize(FArchive& Ar) {
        check(Body);

        Ar << PacketNumber;
        Body->NetSerialize(Ar);
    }
}