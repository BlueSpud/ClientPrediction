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
    };

    template <typename InputType>
    struct FWrappedInput {
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
        void ConsumeInputBundle(const FBundledPackets& Packets);
        void DequeueRecievedInputs();

        void InjectInputsGT();
        void PrepareInputPhysicsThread(const FNetTickInfo& TickInfo);
        void EmitInputs();

    private:
        bool ShouldProduceInput(const FNetTickInfo& TickInfo);

    private:
        FCriticalSection RecvMutex;
        TArray<WrappedInput> Inputs;
        TQueue<WrappedInput> QueuedInputs;

        FCriticalSection SendMutex;
        TArray<WrappedInput> PendingSend; // Inputs that need to be sent at least once
        TArray<WrappedInput> SendWindow; // Inputs that were previously sent (behaves like a sliding window)

    public:
        const InputType& GetCurrentInput() { return CurrentInput.Input; }

    private:
        FCriticalSection GTInputMutex;
        WrappedInput CurrentGTInput{};
        WrappedInput CurrentInput{};
    };

    template <typename Traits>
    void USimInput<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimInput<Traits>::ConsumeInputBundle(const FBundledPackets& Packets) {
        TArray<WrappedInput> BundleInputs;
        Packets.Bundle().Retrieve(BundleInputs);

        FScopeLock RecvLock(&RecvMutex);
        for (WrappedInput& NewInput : BundleInputs) {
            QueuedInputs.Enqueue(MoveTemp(NewInput));
        }
    }

    template <typename Traits>
    void USimInput<Traits>::DequeueRecievedInputs() {
        FScopeLock RecvLock(&RecvMutex);

        WrappedInput NewInput{};
        if (QueuedInputs.IsEmpty()) {
            return;
        }

        // TODO this can be optimized a bit, but the input buffer is probably not going to get too large so it's fine
        while (QueuedInputs.Dequeue(NewInput)) {
            if (Inputs.ContainsByPredicate([&](const WrappedInput& Other) { return Other.ServerTick == NewInput.ServerTick; })) {
                continue;
            }

            Inputs.Add(MoveTemp(NewInput));
        }

        Inputs.Sort([](const WrappedInput& A, const WrappedInput& B) { return A.ServerTick < B.ServerTick; });
    }

    template <typename Traits>
    void USimInput<Traits>::InjectInputsGT() {
        if (SimDelegates != nullptr) {
            FScopeLock GTInputLock(&GTInputMutex);
            SimDelegates->ProduceInputGTDelegate.Broadcast(CurrentGTInput.Input);
        }
    }

    template <typename Traits>
    void USimInput<Traits>::PrepareInputPhysicsThread(const FNetTickInfo& TickInfo) {
        if (TickInfo.SimRole == ENetRole::ROLE_SimulatedProxy) { return; }

        if (USimInput::ShouldProduceInput(TickInfo) && SimDelegates != nullptr) {
            Inputs.Add(CurrentGTInput);

            WrappedInput& NewInput = Inputs.Last();
            NewInput.ServerTick = TickInfo.ServerTick;

            SimDelegates->ModifyInputPTDelegate.Broadcast(NewInput.Input, TickInfo.Dt);
        }
        else if (TickInfo.SimRole == ROLE_Authority) {
            DequeueRecievedInputs();
        }

        // We always use the server tick to find the input to use. This way if the server offset changes, the right input will still be picked.
        for (const WrappedInput& Input : Inputs) {
            if (Input.ServerTick > TickInfo.ServerTick) { break; }

            CurrentInput = Input;
            if (Input.ServerTick == TickInfo.ServerTick) { break; }
        }

        bool bFoundPerfectMatch = CurrentInput.ServerTick == TickInfo.ServerTick;
        check(TickInfo.SimRole != ROLE_AutonomousProxy || bFoundPerfectMatch);

        if (TickInfo.SimRole == ROLE_AutonomousProxy) {
            FScopeLock SendLock(&SendMutex);
            PendingSend.Add(CurrentInput);
        }

        // TODO Make this message a bit better
        if (!bFoundPerfectMatch) {
            UE_LOG(LogTemp, Warning, TEXT("Input fault looking for %d, but best found was %d"), TickInfo.ServerTick, CurrentInput.ServerTick);
        }
    }

    template <typename Traits>
    void USimInput<Traits>::EmitInputs() {
        FScopeLock SendLock(&SendMutex);

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
        const bool bInputNotAlreadySampled = Inputs.IsEmpty() || Inputs.Last().ServerTick < TickInfo.ServerTick;
        const bool bShouldTakeInput = (TickInfo.SimRole == ENetRole::ROLE_AutonomousProxy && !TickInfo.bIsResim)
            || (TickInfo.SimRole == ENetRole::ROLE_Authority && !TickInfo.bHasNetConnection);

        return bInputNotAlreadySampled && bShouldTakeInput;
    }
}
