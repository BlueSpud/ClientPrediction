#pragma once

#include "CoreMinimal.h"
#include "Serialization/MemoryWriter.h"

#include "ClientPredictionModelId.h"
#include "ClientPredictionDataCompleteness.h"

namespace ClientPrediction {
    class IStateProducer;
    class IStateConsumer;

    struct CLIENTPREDICTION_API FSerializedStateData {
        bool bShouldBeReliable = false;
        TMap<EDataCompleteness, TArray<uint8>> Data;
    };

    struct CLIENTPREDICTION_API FTickSnapshot {
        int32 TickNumber = 0;
        TMap<FClientPredictionModelId, FSerializedStateData> StateData;
    };

    struct CLIENTPREDICTION_API FStateManager {
        void RegisterProducerForModel(const FClientPredictionModelId& ModelId, IStateProducer* Producer);
        void UnregisterProducerForModel(const FClientPredictionModelId& ModelId);

        void RegisterConsumerForModel(const FClientPredictionModelId& ModelId, IStateConsumer* Consumer);
        void UnregisterConsumerForModel(const FClientPredictionModelId& ModelId);

        void ProduceData(int32 TickNumber);
        void GetProducedDataForTick(int32 TickNumber, FTickSnapshot& OutSnapshot);
        void ReleasedProducedData(int32 TickNumber);

        Chaos::FReal GetInterpolationTime() const { return InterpolationTime; }
        void SetInterpolationTime(const Chaos::FReal NewInterpolationTime) { InterpolationTime = NewInterpolationTime; }

        float GetEstimatedCurrentServerTick() const { return EstimatedCurrentServerTick; }
        void SetEstimatedCurrentServerTick(const float NewEstimatedTick) { EstimatedCurrentServerTick = NewEstimatedTick; }

        void PushStateToConsumer(int32 TickNumber, const FClientPredictionModelId& ModelId, const TArray<uint8>& Data, const Chaos::FReal ServerTime,
                                 const EDataCompleteness Completeness);

    private:
        FCriticalSection ProducerMutex;
        TMap<FClientPredictionModelId, IStateProducer*> Producers;

        FCriticalSection ConsumerMutex;
        TMap<FClientPredictionModelId, IStateConsumer*> Consumers;

        struct FSerializedState {
            int32 TickNumber = 0;
            FSerializedStateData Data{};
        };

        FCriticalSection ProducedStatesMutex;
        TMap<FClientPredictionModelId, TArray<FSerializedState>> ProducedStates;

        Chaos::FReal InterpolationTime = 0.0;

        /**
         * On remotes, this is the estimated server tick that is currently displayed by sim proxies. This is an estimate because interpolation is calculated with
         * time, rather than discrete ticks and it's possible that they are in-between server ticks.
         */
        float EstimatedCurrentServerTick = INDEX_NONE;
    };

    // State producers
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class IStateProducer {
    public:
        virtual ~IStateProducer() = default;

        /** Produces the serialized state for the given tick and returns true or returns false if there is no state for that tick. */
        virtual bool ProduceStateForTick(const int32 Tick, FSerializedStateData& Data) = 0;
    };

    /** Convenience class that enables producing unserialized states instead */
    template <typename StateType>
    class StateProducerBase : public IStateProducer {
    public:
        virtual bool ProduceStateForTick(const int32 Tick, FSerializedStateData& Data) override final;

        /** Produces the unserialized state for the given tick and returns true or returns false if there is no state for that tick. */
        virtual bool ProduceUnserializedStateForTick(const int32 Tick, StateType& State, bool& bShouldBeReliable) = 0;
    };

    template <typename StateType>
    bool StateProducerBase<StateType>::ProduceStateForTick(const int32 Tick, FSerializedStateData& Data) {
        StateType DeserializedState{};
        if (!ProduceUnserializedStateForTick(Tick, DeserializedState, Data.bShouldBeReliable)) {
            return false;
        }

        Data.Data.Add(EDataCompleteness::kFull, {});
        Data.Data.Add(EDataCompleteness::kStandard, {});

        FMemoryWriter ShortAr = FMemoryWriter(Data.Data[EDataCompleteness::kStandard]);
        DeserializedState.NetSerialize(ShortAr, EDataCompleteness::kStandard);

        FMemoryWriter FullAr = FMemoryWriter(Data.Data[EDataCompleteness::kFull]);
        DeserializedState.NetSerialize(FullAr, EDataCompleteness::kFull);

        return true;
    }

    // State consumers
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class IStateConsumer {
    public:
        virtual ~IStateConsumer() = default;
        virtual void ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const Chaos::FReal ServerTime, const EDataCompleteness Completeness) = 0;
    };

    /** Convenience class that enables consuming unserialized states instead */
    template <typename StateType>
    class StateConsumerBase : public IStateConsumer {
    public:
        virtual void ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const Chaos::FReal ServerTime,
                                         const EDataCompleteness Completeness) override final;

        virtual void ConsumeUnserializedStateForTick(const int32 Tick, const StateType& State, const Chaos::FReal ServerTime) = 0;
    };

    template <typename StateType>
    void StateConsumerBase<StateType>::ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const Chaos::FReal ServerTime,
                                                           const EDataCompleteness Completeness) {
        FMemoryReader Ar(SerializedState);

        StateType DeserializedState{};
        DeserializedState.NetSerialize(Ar, Completeness);

        ConsumeUnserializedStateForTick(Tick, DeserializedState, ServerTime);
    }
}
