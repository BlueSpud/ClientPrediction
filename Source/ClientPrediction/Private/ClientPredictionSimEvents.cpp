#include "ClientPredictionSimEvents.h"

namespace ClientPrediction {
    void USimEvents::ConsumeEvents(const FBundledPackets& Packets, Chaos::FReal SimDt) {
        TArray<FEventLoader> AuthorityEvents;
        Packets.Bundle().Retrieve(AuthorityEvents, FEventLoaderUserdata{Factories, SimDt});
    }

    void USimEvents::ConsumeRemoteSimProxyOffset(const FRemoteSimProxyOffset& Offset) {
        QueuedRemoteSimProxyOffsets.Enqueue(Offset);
    }

    void USimEvents::PreparePrePhysics(const FNetTickInfo& TickInfo) {
        FScopeLock EventLock(&EventMutex);

        // These are emitted over a reliable RPC, so the order is guaranteed. No need to keep track of which offset has been acked.
        FRemoteSimProxyOffset NewRemoteSimProxyOffset{};
        while (QueuedRemoteSimProxyOffsets.Peek(NewRemoteSimProxyOffset)) {
            if (NewRemoteSimProxyOffset.ExpectedAppliedServerTick <= TickInfo.ServerTick) {
                RemoteSimProxyOffset = NewRemoteSimProxyOffset.ServerTickOffset;
                QueuedRemoteSimProxyOffsets.Pop();
            }
            else { break; }
        }
    }

    void USimEvents::ExecuteEvents(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) {
        FScopeLock EventLock(&EventMutex);
        for (auto& FactoryPair : Factories) {
            FactoryPair.Value->ExecuteEvents(ResultsTime, SimProxyOffset, SimRole, HistoryDuration);
        }
    }

    void USimEvents::Rewind(int32 LocalRewindTick) {
        FScopeLock EventLock(&EventMutex);
        for (auto& FactoryPair : Factories) {
            FactoryPair.Value->Rewind(LocalRewindTick);
        }
    }

    void USimEvents::EmitEvents() {
        FScopeLock EventLock(&EventMutex);

        TArray<FEventSaver> Serializers;
        const int32 CurrentLatestEmittedTick = LatestEmittedTick;

        for (auto& FactoryPair : Factories) {
            const int32 FactoryNewestEvent = FactoryPair.Value->EmitEvents(CurrentLatestEmittedTick, Serializers);
            LatestEmittedTick = FMath::Max(FactoryNewestEvent, LatestEmittedTick);
        }

        FBundledPackets EventPackets{};
        EventPackets.Bundle().Store(Serializers, this);
        EmitEventBundle.ExecuteIfBound(EventPackets);
    }
}
