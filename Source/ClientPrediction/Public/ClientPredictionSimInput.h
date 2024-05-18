#pragma once

#include "ClientPredictionDelegate.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionTick.h"

static constexpr int32 kSendWindowSize = 3;

namespace ClientPrediction {
    class USimInputBase {
    public:
        virtual ~USimInputBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitInputBundleDelegate, const FBundledPackets& Bundle)
        FEmitInputBundleDelegate EmitInputBundleDelegate;

        virtual void ConsumeInputBundle(const FBundledPackets& Packets) = 0;
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
        void SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;

    public:
        virtual void ConsumeInputBundle(const FBundledPackets& Packets) override;
        void InjectInputsGT(const FNetTickInfo& TickInfo);
        void PrepareInputPhysicsThread(const FNetTickInfo& TickInfo);
        void EmitInputs();

    private:
        static bool ShouldProduceInput(const FNetTickInfo& TickInfo);

    private:
        TArray<WrappedInput> Inputs;

        TArray<WrappedInput> PendingSend; // Inputs that need to be sent at least once
        TArray<WrappedInput> SendWindow; // Inputs that were previously sent (behaves like a sliding window)

    public:
        const InputType& GetCurrentInput() { return CurrentInput.Input; }

    private:
        WrappedInput CurrentInput{};
    };

    template <typename Traits>
    void USimInput<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimInput<Traits>::ConsumeInputBundle(const FBundledPackets& Packets) {
        // TODO this should be done with an async command
        TArray<WrappedInput> BundleInputs;
        Packets.Bundle().Retrieve(BundleInputs);

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
    void USimInput<Traits>::InjectInputsGT(const FNetTickInfo& TickInfo) {
        check(!TickInfo.bIsResim);

        if (!USimInput::ShouldProduceInput(TickInfo)) {
            return;
        }

        WrappedInput NewInput;
        NewInput.LocalTick = TickInfo.LocalTick;
        NewInput.ServerTick = TickInfo.ServerTick;

        if (SimDelegates != nullptr) {
            SimDelegates->ProduceInputGTDelegate.Broadcast(NewInput.Input, TickInfo.Dt, TickInfo.LocalTick);
        }

        check(Inputs.IsEmpty() || Inputs.Last().ServerTick < TickInfo.ServerTick);
        Inputs.Add(MoveTemp(NewInput));
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

        FBundledPackets Packets{};
        Packets.Bundle().Store(SendWindow);

        EmitInputBundleDelegate.ExecuteIfBound(Packets);
        PendingSend.Reset();
    }

    template <typename Traits>
    bool USimInput<Traits>::ShouldProduceInput(const FNetTickInfo& TickInfo) {
        return (TickInfo.SimRole == ENetRole::ROLE_AutonomousProxy && !TickInfo.bIsResim)
            || (TickInfo.SimRole == ENetRole::ROLE_Authority && !TickInfo.bHasNetConnection);
    }
}
