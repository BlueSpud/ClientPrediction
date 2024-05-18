#pragma once

#include "ClientPredictionDelegate.h"
#include "ClientPredictionTick.h"

#include "ClientPredictionSimInput.generated.h"

static constexpr int32 kSendWindowSize = 3;

USTRUCT()
struct FInputBundle {
    GENERATED_BODY()

    template <typename Packet>
    void Store(TArray<Packet>& Packets);

    template <typename Packet>
    bool Retrieve(TArray<Packet>& Packets) const;

    // Don't need NetIdentical because inputs are only sent client -> server, which is done through an RPC
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

private:
    TArray<uint8> SerializedBits;
    int32 NumberOfBits = -1;
};

template <typename Packet>
void FInputBundle::Store(TArray<Packet>& Packets) {
    check(Packets.Num() < TNumericLimits<uint8>::Max());

    FNetBitWriter Writer(nullptr, TNumericLimits<uint16>::Max());
    uint8 NumPackets = static_cast<uint8>(Packets.Num());
    Writer << NumPackets;

    for (Packet& PacketToWrite : Packets) {
        PacketToWrite.NetSerialize(Writer);
    }

    SerializedBits = *Writer.GetBuffer();
    NumberOfBits = Writer.GetNumBits();
}

template <typename Packet>
bool FInputBundle::Retrieve(TArray<Packet>& Packets) const {
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

namespace ClientPrediction {
    class USimInputBase {
    public:
        virtual ~USimInputBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitInputBundleDelegate, const FInputBundle& Bundle)
        FEmitInputBundleDelegate EmitInputBundleDelegate;

        virtual void ConsumeInputBundle(const FInputBundle& Bundle) = 0;
    };

    template <typename InputType>
    struct FWrappedInput {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        InputType Input;

        void NetSerialize(FArchive& Ar) {
            Ar << ServerTick;
            Input.NetSerialize(Ar);
        }
    };

    template <typename Traits>
    class USimInput : public USimInputBase {
    private:
        using InputType = typename Traits::InputType;
        using WrappedInput = FWrappedInput<InputType>;

    public:
        virtual ~USimInput() override = default;
        virtual void ConsumeInputBundle(const FInputBundle& Bundle) override;

        void SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates);
        void InjectInputsGameThread(const FNetTickInfo& TickInfo);
        void PrepareInputPhysicsThread(const FNetTickInfo& TickInfo);
        void EmitInputs();

    private:
        static bool ShouldProduceInput(const FNetTickInfo& TickInfo);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;
        TArray<WrappedInput> Inputs;

        TArray<WrappedInput> PendingSend; // Inputs that need to be sent at least once
        TArray<WrappedInput> SendWindow; // Inputs that were previously sent (behaves like a sliding window)

    public:
        const InputType& GetCurrentInput() { return CurrentInput; }

    private:
        WrappedInput CurrentInput{};
    };

    template <typename Traits>
    void USimInput<Traits>::ConsumeInputBundle(const FInputBundle& Bundle) {
        TArray<WrappedInput> BundleInputs;
        Bundle.Retrieve(BundleInputs);

        // TODO this can be optimized a bit, but the input buffer is probably not going to get too large so it's fine
        for (WrappedInput& NewInput : BundleInputs) {
            if (Inputs.ContainsByPredicate([&](const WrappedInput& Other) { return Other.ServerTick == NewInput.ServerTick; })) {
                continue;
            }

            NewInput.LocalTick = NewInput.ServerTick;
            Inputs.Add(MoveTemp(NewInput));
        }

        Inputs.Sort([](const WrappedInput& A, const WrappedInput& B) { return A.ServerTick < B.ServerTick; });
    }

    template <typename Traits>
    void USimInput<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimInput<Traits>::InjectInputsGameThread(const FNetTickInfo& TickInfo) {
        check(!TickInfo.bIsResim);

        if (!USimInput::ShouldProduceInput(TickInfo)) {
            return;
        }

        WrappedInput NewInput;
        NewInput.LocalTick = TickInfo.LocalTick;
        NewInput.ServerTick = TickInfo.ServerTick;

        if (SimDelegates != nullptr) {
            SimDelegates->ProduceInputGameThread.Broadcast(NewInput.Input, TickInfo.Dt, TickInfo.LocalTick);
        }

        check(Inputs.IsEmpty() || Inputs.Last().ServerTick < TickInfo.ServerTick);
        Inputs.Add(NewInput);
    }

    template <typename Traits>
    void USimInput<Traits>::PrepareInputPhysicsThread(const FNetTickInfo& TickInfo) {
        if (TickInfo.SimRole == ENetRole::ROLE_SimulatedProxy) { return; }

        for (const WrappedInput& Input : Inputs) {
            if (Input.LocalTick > TickInfo.LocalTick) { break; }

            CurrentInput = Input;
            if (Input.LocalTick == TickInfo.LocalTick) { break; }
        }

        bool bFoundPerfectMatch = CurrentInput.LocalTick == TickInfo.LocalTick;
        check(TickInfo.SimRole != ROLE_AutonomousProxy || bFoundPerfectMatch);

        if (USimInput::ShouldProduceInput(TickInfo)) {
            // TODO modify the input using the current state
        }

        if (TickInfo.SimRole == ROLE_AutonomousProxy) {
            PendingSend.Add(CurrentInput);
        }

        // TODO Make this message a bit better
        if (!bFoundPerfectMatch) {
            UE_LOG(LogTemp, Warning, TEXT("Input fault"));
        }
    }

    template <typename Traits>
    void USimInput<Traits>::EmitInputs() {
        if (PendingSend.IsEmpty()) { return; }

        int32 SendWindowMaxSize = FMath::Max(PendingSend.Num(), kSendWindowSize);
        SendWindow.Append(PendingSend);

        while (SendWindow.Num() > SendWindowMaxSize) {
            SendWindow.RemoveAt(0);
        }

        FInputBundle Bundle{};
        Bundle.Store(SendWindow);

        EmitInputBundleDelegate.ExecuteIfBound(Bundle);
        PendingSend.Reset();
    }

    template <typename Traits>
    bool USimInput<Traits>::ShouldProduceInput(const FNetTickInfo& TickInfo) {
        return (TickInfo.SimRole == ENetRole::ROLE_AutonomousProxy && !TickInfo.bIsResim)
            || (TickInfo.SimRole == ENetRole::ROLE_Authority && !TickInfo.bHasNetConnection);
    }
}
