#pragma once

#include "ClientPredictionInput.h"

namespace ClientPrediction {
    template <typename InputType>
    struct FAutoProxyInputBuf {
        using Wrapper = FInputPacketWrapper<InputType>;

        void QueueInputPacket(const Wrapper& Packet);
        void Ack(const int32 PacketNumber);

        Wrapper* InputForTick(int32 TickNumber);

    private:
        TArray<Wrapper> InputPackets;
        FCriticalSection Mutex;
    };

    template <typename InputType>
    void FAutoProxyInputBuf<InputType>::QueueInputPacket(const Wrapper& Packet) {
        FScopeLock Lock(&Mutex);

        // Packets should come in sequentially on the auto proxy
        if (!InputPackets.IsEmpty()) {
            check(InputPackets.Last().PacketNumber + 1 == Packet.PacketNumber);
        }

        InputPackets.Add(Packet);
    }

    template <typename InputType>
    void FAutoProxyInputBuf<InputType>::Ack(const int32 PacketNumber) {
        FScopeLock Lock(&Mutex);
        while (!InputPackets.IsEmpty()) {
            if (InputPackets[0].PacketNumber <= PacketNumber) {
                InputPackets.RemoveAt(0);
            }
            else { break; }
        }
    }

    template <typename InputType>
    typename FAutoProxyInputBuf<InputType>::Wrapper* FAutoProxyInputBuf<InputType>::InputForTick(int32 TickNumber) {
        FScopeLock Lock(&Mutex);

        for (Wrapper& Packet : InputPackets) {
            if (Packet.PacketNumber == TickNumber) {
                return &Packet;
            }
        }

        return nullptr;
    }
}
