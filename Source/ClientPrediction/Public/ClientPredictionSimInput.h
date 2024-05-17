#pragma once
#include "ClientPredictionDelegate.h"

namespace ClientPrediction {
    struct FInputBundle {
        template <typename Packet>
        void Store(const TArray<Packet>& Packets);

        template <typename Packet>
        bool Retrieve(TArray<Packet>& Packets);

        // Don't need NetIdentical because inputs are only sent client -> server, which is done through an RPC
        bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

    private:
        TArray<uint8> SerializedBits;
        int32 NumberOfBits = -1;
    };

    template <typename Packet>
    void FInputBundle::Store(const TArray<Packet>& Packets) {
        check(Packets.Num() < TNumericLimits<uint8>::Max());

        FNetBitWriter Writer(nullptr, TNumericLimits<uint16>::Max());
        uint8 NumPackets = static_cast<uint8>(Packets.Num());
        Writer << NumPackets;

        for (Packet& PacketToWrite : Packets) {
            PacketToWrite.NetSerialize(Writer);
        }
    }

    template <typename Packet>
    bool FInputBundle::Retrieve(TArray<Packet>& Packets) {
        if (NumberOfBits == -1) { return false; }

        FNetBitReader BitReader(nullptr, SerializedBits.GetData(), NumberOfBits);
        uint8 NumPackets = 0;
        BitReader << NumPackets;

        for (uint8 PacketIdx = 0; PacketIdx < NumPackets; ++PacketIdx) {
            Packets.AddDefaulted();
            Packets.Last().NetSerialize(BitReader);
        }

        return true;
    }

    template <>
    struct TStructOpsTypeTraits<FInputBundle> : public TStructOpsTypeTraitsBase2<FInputBundle> {
        enum {
            WithNetSerializer = true
        };
    };


    class USimInputBase {
    public:
        virtual ~USimInputBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitInputBundleDelegate, const FInputBundle& Bundle)
        FEmitInputBundleDelegate EmitInputBundle;

        virtual void ConsumeInputBundle(const FInputBundle& Bundle) = 0;
    };

    template <typename Traits>
    class USimInput : public USimInputBase {
    private:
        using InputType = typename Traits::InputType;

    public:
        virtual ~USimInput() override = default;
        virtual void ConsumeInputBundle(const FInputBundle& Bundle) override;

        void SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;
    };

    template <typename Traits>
    void USimInput<Traits>::ConsumeInputBundle(const FInputBundle& Bundle) {}

    template <typename Traits>
    void USimInput<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }
}
