#pragma once

#include "ClientPredictionInput.h"

namespace ClientPrediction {
    template <typename InputType>
    struct FAutoProxyInputBuf {
        using Wrapper = FInputPacketWrapper<InputType>;

        explicit FAutoProxyInputBuf(int32 InputBufferMaxSize);

        void QueueInputPacket(const Wrapper& Packet);
        bool InputForTick(int32 TickNumber, Wrapper& OutPacket);

    private:
        TArray<Wrapper> InputPackets;
        int32 InputBufferMaxSize = 0;

        FCriticalSection Mutex;
    };

    template <typename InputType>
    FAutoProxyInputBuf<InputType>::FAutoProxyInputBuf(int32 InputBufferMaxSize) : InputBufferMaxSize(InputBufferMaxSize) {
        InputPackets.Reserve(InputBufferMaxSize);
    }

    template <typename InputType>
    void FAutoProxyInputBuf<InputType>::QueueInputPacket(const Wrapper& Packet) {
        FScopeLock Lock(&Mutex);

        // Packets should come in sequentially on the auto proxy
        if (!InputPackets.IsEmpty()) {
            check(InputPackets.Last().PacketNumber + 1 == Packet.PacketNumber);

            if (InputPackets.Num() == InputBufferMaxSize) {
                InputPackets.RemoveAt(0);
            }
        }

        InputPackets.Add(Packet);
    }

    template <typename InputType>
    bool FAutoProxyInputBuf<InputType>::InputForTick(int32 TickNumber, Wrapper& OutPacket) {
        FScopeLock Lock(&Mutex);

        for (const Wrapper& Packet : InputPackets) {
            if (Packet.PacketNumber == TickNumber) {
                OutPacket = Packet;
                return true;
            }
        }

        return false;
    }
}
