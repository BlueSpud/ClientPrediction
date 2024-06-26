﻿#pragma once

#include "ClientPredictionCVars.h"
#include "ClientPredictionDelegate.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionTick.h"

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

        void NetSerialize(FArchive& Ar, void* Userdata) {
            Ar << ServerTick;
            Input.NetSerialize(Ar);
        }
    };

    template <typename Traits>
    class USimInput : public USimInputBase {
    private:
        using InputType = typename Traits::InputType;
        using StateType = typename Traits::StateType;
        using WrappedInput = FWrappedInput<InputType>;

    public:
        virtual ~USimInput() override = default;
        void SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates);
        void SetBufferSize(int32 BufferSize);

    private:
        TSharedPtr<FSimDelegates<Traits>> SimDelegates;

        int32 BufferIndex(int32 ServerTick);

    public:
        void ConsumeInputBundle(const FBundledPackets& Packets);

        void InjectInputsGT();
        void PreparePrePhysics(const FNetTickInfo& TickInfo, const StateType& PrevState);
        void EmitInputs();

    private:
        bool ShouldProduceInput(const FNetTickInfo& TickInfo);

    private:
        TArray<WrappedInput> Inputs;
        TQueue<WrappedInput> RecvQueue;

        FCriticalSection SendMutex;
        TArray<WrappedInput> PendingSend; // Inputs that need to be sent at least once
        TArray<WrappedInput> SendWindow; // Inputs that were previously sent (behaves like a sliding window)

    public:
        const InputType& GetCurrentInput() { return CurrentInput.Input; }

    private:
        FCriticalSection GTInputMutex;
        WrappedInput CurrentInput{};
        InputType CurrentGTInput{};

        int32 LatestProducedInput = INDEX_NONE;
    };

    template <typename Traits>
    void USimInput<Traits>::SetSimDelegates(const TSharedPtr<FSimDelegates<Traits>>& NewSimDelegates) {
        SimDelegates = NewSimDelegates;
    }

    template <typename Traits>
    void USimInput<Traits>::SetBufferSize(int32 BufferSize) {
        while (Inputs.Num() < BufferSize) {
            Inputs.AddDefaulted();
            Inputs.Last().ServerTick = TNumericLimits<int32>::Min();
        }
    }

    template <typename Traits>
    int32 USimInput<Traits>::BufferIndex(int32 ServerTick) {
        const int32 BufferSize = Inputs.Num();
        return (ServerTick % BufferSize + BufferSize) % BufferSize;
    }

    template <typename Traits>
    void USimInput<Traits>::ConsumeInputBundle(const FBundledPackets& Packets) {
        TArray<WrappedInput> BundleInputs;
        Packets.Bundle().Retrieve<>(BundleInputs, this);

        for (WrappedInput& NewInput : BundleInputs) {
            const int32 NewBufferIndex = BufferIndex(NewInput.ServerTick);
            if (Inputs[NewBufferIndex].ServerTick < NewInput.ServerTick) {
                Inputs[NewBufferIndex] = NewInput;
            }
        }
    }

    template <typename Traits>
    void USimInput<Traits>::InjectInputsGT() {
        FScopeLock GTInputLock(&GTInputMutex);
        if (SimDelegates != nullptr) {
            CurrentGTInput = {};
            SimDelegates->ProduceInputGTDelegate.Broadcast(CurrentGTInput);
        }
    }

    template <typename Traits>
    void USimInput<Traits>::PreparePrePhysics(const FNetTickInfo& TickInfo, const StateType& PrevState) {
        if (TickInfo.SimRole == ENetRole::ROLE_SimulatedProxy) { return; }

        if (USimInput::ShouldProduceInput(TickInfo) && SimDelegates != nullptr) {
            WrappedInput& NewInput = Inputs[BufferIndex(TickInfo.ServerTick)];
            NewInput.ServerTick = TickInfo.ServerTick;

            FScopeLock GTInputLock(&GTInputMutex);
            NewInput.Input = CurrentGTInput;

            SimDelegates->ModifyInputPTDelegate.Broadcast(NewInput.Input, PrevState, FSimTickInfo(TickInfo));
            LatestProducedInput = FMath::Max(TickInfo.ServerTick, LatestProducedInput);
        }

        // We always use the server tick to find the input to use. This way if the server offset changes, the right input will still be picked.
        int32 BestInputIndex = INDEX_NONE;
        for (int32 Index = 0; Index < Inputs.Num(); ++Index) {
            const WrappedInput& Input = Inputs[Index];
            if (Input.ServerTick > TickInfo.ServerTick) { continue; }

            if (Input.ServerTick == TickInfo.ServerTick) {
                BestInputIndex = Index;
                break;
            }

            if (BestInputIndex == INDEX_NONE || Inputs[BestInputIndex].ServerTick < Input.ServerTick) {
                BestInputIndex = Index;
            }
        }

        if (BestInputIndex != INDEX_NONE) {
            CurrentInput = Inputs[BestInputIndex];
        }

        if (TickInfo.SimRole == ROLE_AutonomousProxy) {
            FScopeLock SendLock(&SendMutex);
            PendingSend.Add(CurrentInput);
        }
    }

    template <typename Traits>
    void USimInput<Traits>::EmitInputs() {
        FScopeLock SendLock(&SendMutex);
        if (PendingSend.IsEmpty()) {
            return;
        }

        int32 SendWindowMaxSize = FMath::Max(PendingSend.Num(), ClientPredictionInputWindowSize);
        SendWindow.Append(PendingSend);

        while (SendWindow.Num() > SendWindowMaxSize) {
            SendWindow.RemoveAt(0);
        }

        FBundledPackets Packets{};
        Packets.Bundle().Store(SendWindow, this);

        EmitInputBundleDelegate.ExecuteIfBound(Packets);
        PendingSend.Reset();
    }

    template <typename Traits>
    bool USimInput<Traits>::ShouldProduceInput(const FNetTickInfo& TickInfo) {
        const bool bShouldTakeInput =
            (TickInfo.SimRole == ENetRole::ROLE_AutonomousProxy && !TickInfo.bIsResim) ||
            (TickInfo.SimRole == ENetRole::ROLE_Authority && !TickInfo.bHasNetConnection);

        return (LatestProducedInput < TickInfo.ServerTick) && bShouldTakeInput;
    }
}
