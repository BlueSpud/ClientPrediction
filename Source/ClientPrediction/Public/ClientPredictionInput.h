#pragma once

namespace ClientPrediction {
    struct FInputPacketWrapper {
        int32 PacketNumber = INDEX_NONE;

        /**
         * This uses a void pointer so that the drivers can avoid templates. The model has templates and handles
         * creation of the bodies, so it can cast to them as needed. The shared pointers are smart enough to
         * know how to destroy the bodies as well, so no manual casting / deleting is needed for that.
         */
        TSharedPtr<void> Body = nullptr;

        void NetSerialize(FArchive& Ar);
    };

    inline void FInputPacketWrapper::NetSerialize(FArchive& Ar) {
        Ar << PacketNumber;
    }
}