#include "Data/ClientPredictionStateManager.h"

namespace ClientPrediction {
    void FStateManager::RegisterProducerForModel(const FClientPredictionModelId& ModelId, IStateProducer* Producer) {
        FScopeLock ProducerLock(&ProducerMutex);

        // Only one producer per model is allowed
        check(!Producers.Contains(ModelId));

        Producers.Add(ModelId, Producer);
        ProducedStates.Add(ModelId, {});
    }

    void FStateManager::UnregisterProducerForModel(const FClientPredictionModelId& ModelId) {
        FScopeLock ProducerLock(&ProducerMutex);

        if (Producers.Contains(ModelId)) {
            Producers.Remove(ModelId);
            ProducedStates.Remove(ModelId);
        }
    }

    void FStateManager::RegisterConsumerForModel(const FClientPredictionModelId& ModelId, IStateConsumer* Consumer) {
        FScopeLock ConsumerLock(&ConsumerMutex);

        // Only one producer per model is allowed
        check(!Consumers.Contains(ModelId));

        Consumers.Add(ModelId, Consumer);
    }

    void FStateManager::UnregisterConsumerForModel(const FClientPredictionModelId& ModelId) {
        FScopeLock ConsumerLock(&ConsumerMutex);

        if (Consumers.Contains(ModelId)) {
            Consumers.Remove(ModelId);
        }
    }

    void FStateManager::ProduceData(int32 TickNumber) {
        FScopeLock ProducerLock(&ProducerMutex);
        FScopeLock ProducedStateMutex(&ProducedStatesMutex);

        for (auto& Pair : Producers) {
            FSerializedState ProducedState{};
            ProducedState.TickNumber = TickNumber;

            if (Pair.Value->ProduceStateForTick(TickNumber, ProducedState.Data)) {
                ProducedStates[Pair.Key].Push(MoveTemp(ProducedState));
            }
        }
    }

    void FStateManager::GetProducedDataForTick(const int32 TickNumber, FTickSnapshot& OutSnapshot) {
        FScopeLock ProducedStateMutex(&ProducedStatesMutex);

        OutSnapshot = {};
        OutSnapshot.TickNumber = TickNumber;

        for (auto& Pair : ProducedStates) {
            const TArray<FSerializedState>& ProducedData = Pair.Value;
            const FSerializedState* FrameState = ProducedData.FindByPredicate([&](const FSerializedState& State) {
                return State.TickNumber == TickNumber;
            });

            if (FrameState != nullptr) {
                OutSnapshot.StateData.Add(Pair.Key, FrameState->Data);
            }
        }
    }

    void FStateManager::ReleasedProducedData(const int32 TickNumber) {
        FScopeLock ProducedStateMutex(&ProducedStatesMutex);

        for (auto& Pair : ProducedStates) {
            TArray<FSerializedState>& ProducedData = Pair.Value;
            while (!ProducedData.IsEmpty() && ProducedData[0].TickNumber <= TickNumber) {
                ProducedData.RemoveAt(0);
            }
        }
    }

    void FStateManager::PushStateToConsumer(int32 TickNumber, const FClientPredictionModelId& ModelId, const TArray<uint8>& Data, const Chaos::FReal ServerTime,
                                            const ERelevancy Relevancy) {
        FScopeLock ConsumerLock(&ConsumerMutex);

        if (!Consumers.Contains(ModelId)) {
            return;
        }

        Consumers[ModelId]->ConsumeStateForTick(TickNumber, Data, ServerTime, Relevancy);
    }
};
