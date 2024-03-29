﻿#pragma once

#include "ClientPredictionSettings.h"

#include "ClientPredictionInput.h"

namespace ClientPrediction {
    template <typename InputType>
    struct FAuthInputBuf {
        using Wrapper = FInputPacketWrapper<InputType>;

        FAuthInputBuf(const UClientPredictionSettings* Settings, bool bIsAuthorityGeneratingInput) : Settings(Settings),
                                                                                                     bIsAuthorityGeneratingInput(bIsAuthorityGeneratingInput) {}

        void QueueInputPackets(const TArray<Wrapper>& Packets);
        void GetNextInputPacket(Wrapper& OutPacket, Chaos::FReal Dt);

    public:
        uint32 GetBufferSize() {
            FScopeLock Lock(&Mutex);
            return InputPackets.Num();
        }

        uint32 GetNumRecentlyDroppedInputPackets() {
            FScopeLock Lock(&Mutex);
            return DroppedInputPacketIndices.Num();
        }

    private:
        void ConsumeFirstPacket(Wrapper& OutPacket);
        void TrimDroppedPacketsBuffer(const int32 ExpectedPacketNumber);

        TArray<Wrapper> InputPackets;
        Wrapper LastInputPacket{};

        const UClientPredictionSettings* Settings = nullptr;
        TArray<int32> DroppedInputPacketIndices;
        bool bIsAuthorityGeneratingInput = false;

        FCriticalSection Mutex;
    };

    template <typename InputType>
    void FAuthInputBuf<InputType>::QueueInputPackets(const TArray<Wrapper>& Packets) {
        FScopeLock Lock(&Mutex);

        for (const Wrapper& Packet : Packets) {
            if (Packet.PacketNumber <= LastInputPacket.PacketNumber) { continue; }

            const bool bAlreadyHasPacket = InputPackets.ContainsByPredicate([&](const Wrapper& Candidate) {
                return Candidate.PacketNumber == Packet.PacketNumber;
            });

            if (!bAlreadyHasPacket) {
                InputPackets.Add(Packet);
            }
        }

        InputPackets.Sort([](const Wrapper& A, const Wrapper& B) {
            return A.PacketNumber < B.PacketNumber;
        });
    }

    template <typename InputType>
    void FAuthInputBuf<InputType>::GetNextInputPacket(Wrapper& OutPacket, Chaos::FReal Dt) {
        FScopeLock Lock(&Mutex);

        if (LastInputPacket.PacketNumber == INDEX_NONE) {
            if (!InputPackets.IsEmpty()) {
                ConsumeFirstPacket(OutPacket);
                return;
            }
        }

        const int32 ExpectedPacketNumber = LastInputPacket.PacketNumber + 1;
        TrimDroppedPacketsBuffer(ExpectedPacketNumber);

        if (!InputPackets.IsEmpty()) {
            if (InputPackets[0].PacketNumber == ExpectedPacketNumber) {
                ConsumeFirstPacket(OutPacket);
                return;
            }
        }

        LastInputPacket.PacketNumber = ExpectedPacketNumber;
        DroppedInputPacketIndices.Add(ExpectedPacketNumber);
        OutPacket = LastInputPacket;
    }

    template <typename InputType>
    void FAuthInputBuf<InputType>::ConsumeFirstPacket(Wrapper& OutPacket) {
        OutPacket = LastInputPacket = InputPackets[0];
        InputPackets.RemoveAt(0);
    }

    template <typename InputType>
    void FAuthInputBuf<InputType>::TrimDroppedPacketsBuffer(const int32 ExpectedPacketNumber) {
        while (!DroppedInputPacketIndices.IsEmpty() && DroppedInputPacketIndices[0] < ExpectedPacketNumber - Settings->DroppedPacketMemoryTickLength) {
            DroppedInputPacketIndices.RemoveAt(0);
        }
    }
}
