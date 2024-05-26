#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionNetSerialization.h"
#include "ClientPredictionSimProxy.h"
#include "ClientPredictionTick.h"

// For now events are ONLY predicted on auto proxies and replicated on sim proxies. In the future we might need to change this
// so that the server can inform an auto proxy has executed event it mispredicted. It might also make sense to be able to rewind events as well.

namespace ClientPrediction {
    using EventId = uint8;

    struct FEventIds {
        static EventId kNextEventId;

        template <typename EventType>
        static EventId GetId() {
            static EventId kEventId = ++kNextEventId;
            return kEventId;
        }
    };

    EventId FEventIds::kNextEventId = 0;

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

    class CLIENTPREDICTION_API USimEvents {
    public:
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

        DECLARE_DELEGATE_OneParam(FEmitEventBundleDelegate, const FBundledPackets& Bundle)
        FEmitEventBundleDelegate EmitEventBundle;

    private:
        TMap<EventId, TUniquePtr<FEventFactoryBase>> Factories;

        FCriticalSection EventMutex;
        TArray<TUniquePtr<FEventWrapperBase>> Events;

        // Relevant only for the authorities
        int32 LatestEmittedTick = INDEX_NONE;
        TQueue<FRemoteSimProxyOffset> QueuedRemoteSimProxyOffsets;
        int32 RemoteSimProxyOffset = 0;
    };

    template <typename EventType>
    TMulticastDelegate<void(const EventType&, Chaos::FReal)>& USimEvents::RegisterEvent() {
        const EventId EventId = FEventIds::GetId<EventType>();
        TUniquePtr<FEventFactory<EventType>> Handler = MakeUnique<FEventFactory<EventType>>(EventId);

        TMulticastDelegate<void(const EventType&, Chaos::FReal)>& Delegate = Handler->Delegate;
        Factories.Add(EventId, MoveTemp(Handler));

        return Delegate;
    }

    template <typename EventType>
    void USimEvents::DispatchEvent(const FNetTickInfo& TickInfo, const EventType& NewEvent) {
        FScopeLock EventLock(&EventMutex);

        const EventId EventId = FEventIds::GetId<EventType>();
        if (!Factories.Contains(EventId)) { return; }

        Events.Emplace(Factories[EventId]->CreateEvent(TickInfo, RemoteSimProxyOffset, &NewEvent));
    }
}
