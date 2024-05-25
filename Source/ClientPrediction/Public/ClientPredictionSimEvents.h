#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionSimProxy.h"
#include "ClientPredictionTick.h"

// For now events are ONLY predicted on auto proxies and replicated on sim proxies. In the future we might need to change this
// so that the server can inform an auto proxy has executed event it mispredicted. It might also make sense to be able to rewind events as well.

namespace ClientPrediction {
    using EventId = uint8;

    template <typename Traits>
    struct FEventIds {
        static EventId kNextEventId;

        template <typename EventType>
        static EventId GetId() {
            static EventId kEventId = ++kNextEventId;
            return kEventId;
        }
    };

    template <typename Traits>
    EventId FEventIds<Traits>::kNextEventId = 0;

    struct FEventWrapperBase {
        virtual ~FEventWrapperBase() = default;

        EventId EventId = INDEX_NONE;
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        Chaos::FReal ExecutionTime = 0.0;
        Chaos::FReal TimeSincePredicted = 0.0;

        virtual bool ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) = 0;
        virtual void NetSerialize(FArchive& Ar) = 0;
    };

    template <typename EventType>
    struct FEventWrapper : public FEventWrapperBase {
        TMulticastDelegate<void(const EventType&, Chaos::FReal)>* Delegate = nullptr;
        EventType Event{};

        virtual bool ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) override;
        virtual void NetSerialize(FArchive& Ar) override;
    };

    template <typename EventType>
    bool FEventWrapper<EventType>::ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) {
        if (Delegate == nullptr) { return true; }

        Chaos::FReal AdjustedResultsTime = SimRole != ROLE_SimulatedProxy ? ResultsTime : ResultsTime + SimProxyOffset;
        if (AdjustedResultsTime < ExecutionTime) {
            return false;
        }

        Delegate->Broadcast(Event, TimeSincePredicted);
        return true;
    }

    template <typename EventType>
    void FEventWrapper<EventType>::NetSerialize(FArchive& Ar) {
        // EventId is not serialized here because when deserializing it from the authority a factory needs to create this object first.
        Ar << ServerTick;

        Event.NetSerialize(Ar);
    }

    struct FEventFactoryBase {
        virtual ~FEventFactoryBase() = default;
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(const FNetTickInfo& TickInfo, int32 RemoteSimProxyOffset, const void* Data) = 0;
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(FArchive& Ar, Chaos::FReal SimDt) = 0;
    };

    template <typename EventType>
    struct FEventFactory : public FEventFactoryBase {
        using WrappedEvent = FEventWrapper<EventType>;

        FEventFactory(int32 EventId) : EventId(EventId) {}
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(const FNetTickInfo& TickInfo, int32 RemoteSimProxyOffset, const void* Data) override;
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(FArchive& Ar, Chaos::FReal SimDt) override;

        TMulticastDelegate<void(const EventType&, Chaos::FReal)> Delegate;
        int32 EventId = INDEX_NONE;
    };

    template <typename EventType>
    TUniquePtr<FEventWrapperBase> FEventFactory<EventType>::CreateEvent(const FNetTickInfo& TickInfo, int32 RemoteSimProxyOffset, const void* Data) {
        TUniquePtr<WrappedEvent> NewEvent = MakeUnique<WrappedEvent>();
        NewEvent->EventId = EventId;
        NewEvent->LocalTick = TickInfo.LocalTick;
        NewEvent->ServerTick = TickInfo.ServerTick;

        NewEvent->ExecutionTime = TickInfo.StartTime;
        NewEvent->TimeSincePredicted = FMath::Abs(static_cast<Chaos::FReal>(FMath::Min(RemoteSimProxyOffset, 0)) * TickInfo.Dt);

        NewEvent->Delegate = &Delegate;
        NewEvent->Event = *static_cast<const EventType*>(Data);

        return NewEvent;
    }

    template <typename EventType>
    TUniquePtr<FEventWrapperBase> FEventFactory<EventType>::CreateEvent(FArchive& Ar, Chaos::FReal SimDt) {
        TUniquePtr<WrappedEvent> NewEvent = MakeUnique<WrappedEvent>();
        NewEvent->EventId = EventId;
        NewEvent->Delegate = &Delegate;

        NewEvent->NetSerialize(Ar);

        NewEvent->ExecutionTime = static_cast<Chaos::FReal>(NewEvent->ServerTick) * SimDt;

        return NewEvent;
    }

    struct FEventSaver {
        FEventSaver(const TUniquePtr<FEventWrapperBase>& Event) : Event(Event) {}

        void NetSerialize(FArchive& Ar, void* Userdata) {
            check(Ar.IsSaving());

            Ar << Event->EventId;
            Event->NetSerialize(Ar);
        }

    private:
        const TUniquePtr<FEventWrapperBase>& Event;
    };

    struct FEventLoaderUserdata {
        const TMap<EventId, TUniquePtr<FEventFactoryBase>>& Factories;
        Chaos::FReal SimDt;
    };

    struct FEventLoader {
        void NetSerialize(FArchive& Ar, const FEventLoaderUserdata& Userdata);
        TUniquePtr<FEventWrapperBase> Event;
    };

    inline void FEventLoader::NetSerialize(FArchive& Ar, const FEventLoaderUserdata& Userdata) {
        check(Ar.IsLoading());

        EventId EventId;
        Ar << EventId;

        check(Userdata.Factories.Contains(EventId));
        Event = Userdata.Factories[EventId]->CreateEvent(Ar, Userdata.SimDt);
    }

    class USimEventsBase {
    public:
        virtual ~USimEventsBase() = default;

        DECLARE_DELEGATE_OneParam(FEmitEventBundleDelegate, const FBundledPackets& Bundle)
        FEmitEventBundleDelegate EmitEventBundle;
    };

    template <typename Traits>
    class USimEvents : public USimEventsBase {
    public:
        virtual ~USimEvents() override = default;


        template <typename EventType>
        TMulticastDelegate<void(const EventType&, Chaos::FReal)>& RegisterEvent();

        template <typename Event>
        void DispatchEvent(const FNetTickInfo& TickInfo, const Event& NewEvent);

        void ConsumeEvents(const FBundledPackets& Packets, Chaos::FReal SimDt);
        void ConsumeRemoteSimProxyOffset(const FRemoteSimProxyOffset& Offset);

        void PreparePrePhysics(const FNetTickInfo& TickInfo);
        void ExecuteEvents(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole);
        void Rewind(int32 LocalRewindTick);

        void EmitEvents();

    private:
        TMap<EventId, TUniquePtr<FEventFactoryBase>> Factories;

        FCriticalSection EventMutex;
        TArray<TUniquePtr<FEventWrapperBase>> Events;

        // Relevant only for the authorities
        int32 LatestEmittedTick = INDEX_NONE;
        TQueue<FRemoteSimProxyOffset> QueuedRemoteSimProxyOffsets;
        int32 RemoteSimProxyOffset = 0;
    };

    template <typename Traits>
    template <typename EventType>
    TMulticastDelegate<void(const EventType&, Chaos::FReal)>& USimEvents<Traits>::RegisterEvent() {
        const EventId EventId = FEventIds<Traits>::template GetId<EventType>();
        TUniquePtr<FEventFactory<EventType>> Handler = MakeUnique<FEventFactory<EventType>>(EventId);

        TMulticastDelegate<void(const EventType&, Chaos::FReal)>& Delegate = Handler->Delegate;
        Factories.Add(EventId, MoveTemp(Handler));

        return Delegate;
    }

    template <typename Traits>
    template <typename EventType>
    void USimEvents<Traits>::DispatchEvent(const FNetTickInfo& TickInfo, const EventType& NewEvent) {
        FScopeLock EventLock(&EventMutex);

        const EventId EventId = FEventIds<Traits>::template GetId<EventType>();
        if (!Factories.Contains(EventId)) { return; }

        Events.Emplace(Factories[EventId]->CreateEvent(TickInfo, RemoteSimProxyOffset, &NewEvent));
    }

    template <typename Traits>
    void USimEvents<Traits>::ConsumeEvents(const FBundledPackets& Packets, Chaos::FReal SimDt) {
        TArray<FEventLoader> AuthorityEvents;
        Packets.Bundle().Retrieve(AuthorityEvents, FEventLoaderUserdata{Factories, SimDt});

        for (FEventLoader& Loader : AuthorityEvents) {
            Events.Add(MoveTemp(Loader.Event));
        }
    }

    template <typename Traits>
    void USimEvents<Traits>::ConsumeRemoteSimProxyOffset(const FRemoteSimProxyOffset& Offset) {
        QueuedRemoteSimProxyOffsets.Enqueue(Offset);
    }

    template <typename Traits>
    void USimEvents<Traits>::PreparePrePhysics(const FNetTickInfo& TickInfo) {
        // These are emitted over a reliable RPC, so the order is guaranteed. No need to keep track of which offset has been acked.
        FRemoteSimProxyOffset NewRemoteSimProxyOffset{};
        while (QueuedRemoteSimProxyOffsets.Peek(NewRemoteSimProxyOffset)) {
            if (NewRemoteSimProxyOffset.ExpectedAppliedServerTick <= TickInfo.ServerTick) {
                RemoteSimProxyOffset = NewRemoteSimProxyOffset.ServerTickOffset;
                QueuedRemoteSimProxyOffsets.Pop();
            } else { break; }
        }
    }

    template <typename Traits>
    void USimEvents<Traits>::ExecuteEvents(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) {
        FScopeLock EventLock(&EventMutex);
        for (int32 EventIdx = 0; EventIdx < Events.Num();) {
            if (Events[EventIdx]->ExecuteIfNeeded(ResultsTime, SimProxyOffset, SimRole)) {
                Events.RemoveAt(EventIdx);
                continue;
            }

            ++EventIdx;
        }
    }

    template <typename Traits>
    void USimEvents<Traits>::Rewind(int32 LocalRewindTick) {
        FScopeLock EventLock(&EventMutex);
        for (int32 EventIdx = 0; EventIdx < Events.Num();) {
            if (Events[EventIdx]->LocalTick >= LocalRewindTick) {
                Events.RemoveAt(EventIdx);
            }
            else { ++EventIdx; }
        }
    }

    template <typename Traits>
    void USimEvents<Traits>::EmitEvents() {
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
