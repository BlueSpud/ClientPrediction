#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionModelId.h"
#include "ClientPredictionRelevancy.h"

namespace ClientPrediction {
    class IStateProducer;
    class IStateConsumer;

    struct CLIENTPREDICTION_API FSerializedStateData {
        // TODO replace with ERelevancy
        // TODO add reliable in here
        TArray<uint8> ShortData{};
        TArray<uint8> FullData{};
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

        void PushStateToConsumer(int32 TickNumber, const FClientPredictionModelId& ModelId, const TArray<uint8>& Data, const ERelevancy Relevancy);

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
        virtual bool ProduceUnserializedStateForTick(const int32 Tick, StateType& State) = 0;
    };

    template <typename StateType>
    bool StateProducerBase<StateType>::ProduceStateForTick(const int32 Tick, FSerializedStateData& Data) {
        StateType DeserializedState{};
        if (!ProduceUnserializedStateForTick(Tick, DeserializedState)) {
            return false;
        }

        // TODO replace with ERelevancy
        FMemoryWriter ShortAr = FMemoryWriter(Data.ShortData);
        DeserializedState.NetSerialize(ShortAr, false);

        FMemoryWriter FullAr = FMemoryWriter(Data.FullData);
        DeserializedState.NetSerialize(FullAr, true);

        return true;
    }

    // State consumers
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class IStateConsumer {
    public:
        virtual ~IStateConsumer() = default;
        virtual void ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const ERelevancy Relevancy) = 0;
    };

    /** Convenience class that enables consuming unserialized states instead */
    template <typename StateType>
    class StateConsumerBase : public IStateConsumer {
    public:
        virtual void ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const ERelevancy Relevancy) override final;
        virtual void ConsumeUnserializedStateForTick(const int32 Tick, const StateType& State) = 0;
    };

    template <typename StateType>
    void StateConsumerBase<StateType>::ConsumeStateForTick(const int32 Tick, const TArray<uint8>& SerializedState, const ERelevancy Relevancy) {
        FMemoryReader Ar(SerializedState);

        StateType DeserializedState{};
        DeserializedState.NetSerialize(Ar, Relevancy == ERelevancy::kAutoProxy);

        ConsumeUnserializedStateForTick(Tick, DeserializedState);
    }
}
