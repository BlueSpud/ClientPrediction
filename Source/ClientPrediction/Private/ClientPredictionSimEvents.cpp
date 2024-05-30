#include "ClientPredictionSimEvents.h"

namespace ClientPrediction {
    void USimEvents::ConsumeEvents(const FBundledPackets& Packets, Chaos::FReal SimDt) {
        TArray<FEventLoader> AuthorityEvents;
        Packets.Bundle().Retrieve(AuthorityEvents, FEventLoaderUserdata{Factories, SimDt});

        for (FEventLoader& Loader : AuthorityEvents) {
            Events.Add(MoveTemp(Loader.Event));
        }
    }

    void USimEvents::ConsumeRemoteSimProxyOffset(const FRemoteSimProxyOffset& Offset) {
        QueuedRemoteSimProxyOffsets.Enqueue(Offset);
    }

    void USimEvents::PreparePrePhysics(const FNetTickInfo& TickInfo) {
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
        for (int32 EventIdx = 0; EventIdx < Events.Num();) {
            if (Events[EventIdx]->ExecuteIfNeeded(ResultsTime, SimProxyOffset, SimRole)) {
                Events.RemoveAt(EventIdx);
                continue;
            }

            ++EventIdx;
        }
    }

    void USimEvents::Rewind(int32 LocalRewindTick) {
        FScopeLock EventLock(&EventMutex);
        for (int32 EventIdx = 0; EventIdx < Events.Num();) {
            if (Events[EventIdx]->LocalTick >= LocalRewindTick) {
                Events.RemoveAt(EventIdx);
            }
            else { ++EventIdx; }
        }
    }

    void USimEvents::EmitEvents() {
        FScopeLock EventLock(&EventMutex);
        if (Events.IsEmpty() || Events.Last()->ServerTick <= LatestEmittedTick) {
            return;
        }

        TArray<FEventSaver> Serializers;
        for (const TUniquePtr<FEventWrapperBase>& Event : Events) {
            if (Event->ServerTick > LatestEmittedTick) {
                Serializers.Add(FEventSaver(Event));
            }
        }

        FBundledPackets EventPackets{};
        EventPackets.Bundle().Store(Serializers, this);
        EmitEventBundle.ExecuteIfBound(EventPackets);

        LatestEmittedTick = Events.Last()->ServerTick;
    }
}
