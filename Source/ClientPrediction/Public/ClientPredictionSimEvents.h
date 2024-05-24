#pragma once

#include "CoreMinimal.h"
#include "ClientPredictionTick.h"

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
    uint8 FEventIds<Traits>::kNextEventId = 0;

    struct FEventWrapperBase {
        virtual ~FEventWrapperBase() = default;

        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        Chaos::FReal ExecutionTime = 0.0;
        bool bHasBeenExecuted = false;

        virtual void ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) = 0;
        virtual void NetSerialize(FArchive& Ar) = 0;
    };

    template <typename EventType>
    struct FEventWrapper : public FEventWrapperBase {
        TMulticastDelegate<void(const EventType&)>* Delegate = nullptr;
        EventType Event{};

        virtual void ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) override;
        virtual void NetSerialize(FArchive& Ar) override;
    };

    template <typename EventType>
    void FEventWrapper<EventType>::ExecuteIfNeeded(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) {
        if (Delegate == nullptr || bHasBeenExecuted) { return; }

        Chaos::FReal AdjustedResultsTime = SimRole != ROLE_SimulatedProxy ? ResultsTime : ResultsTime + SimProxyOffset;
        if (AdjustedResultsTime < ExecutionTime) {
            return;
        }

        bHasBeenExecuted = true;
        Delegate->Broadcast(Event);
    }

    template <typename EventType>
    void FEventWrapper<EventType>::NetSerialize(FArchive& Ar) {
        Ar << ServerTick;
        Event.NetSerialize(Ar);
    }

    struct FEventFactoryBase {
        virtual ~FEventFactoryBase() = default;
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(const FNetTickInfo& TickInfo, const void* Data) = 0;
    };

    template <typename EventType>
    struct FEventFactory : public FEventFactoryBase {
        using WrappedEvent = FEventWrapper<EventType>;
        virtual TUniquePtr<FEventWrapperBase> CreateEvent(const FNetTickInfo& TickInfo, const void* Data) override;

        TMulticastDelegate<void(const EventType&)> Delegate;
    };

    template <typename EventType>
    TUniquePtr<FEventWrapperBase> FEventFactory<EventType>::CreateEvent(const FNetTickInfo& TickInfo, const void* Data) {
        TUniquePtr<WrappedEvent> NewEvent = MakeUnique<WrappedEvent>();
        NewEvent->LocalTick = TickInfo.LocalTick;
        NewEvent->ServerTick = TickInfo.ServerTick;

        NewEvent->ExecutionTime = TickInfo.StartTime;
        NewEvent->bHasBeenExecuted = false;

        NewEvent->Delegate = &Delegate;
        NewEvent->Event = *static_cast<const EventType*>(Data);

        return NewEvent;
    }

    class USimEventsBase {
    public:
        virtual ~USimEventsBase() = default;
    };

    template <typename Traits>
    class USimEvents : public USimEventsBase {
    public:
        virtual ~USimEvents() override = default;

        template <typename EventType>
        TMulticastDelegate<void(const EventType&)>& RegisterEvent();

        template <typename Event>
        void DispatchEvent(const FNetTickInfo& TickInfo, const Event& NewEvent);
        void ExecuteEvents(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole);

    private:
        TMap<EventId, TUniquePtr<FEventFactoryBase>> Handlers;
        TArray<TUniquePtr<FEventWrapperBase>> Events;
    };

    template <typename Traits>
    template <typename EventType>
    TMulticastDelegate<void(const EventType&)>& USimEvents<Traits>::RegisterEvent() {
        const EventId EventId = FEventIds<Traits>::template GetId<EventType>();
        TUniquePtr<FEventFactory<EventType>> Handler = MakeUnique<FEventFactory<EventType>>();

        TMulticastDelegate<void(const EventType&)>& Delegate = Handler->Delegate;
        Handlers.Add(EventId, MoveTemp(Handler));

        return Delegate;
    }

    template <typename Traits>
    template <typename EventType>
    void USimEvents<Traits>::DispatchEvent(const FNetTickInfo& TickInfo, const EventType& NewEvent) {
        const EventId EventId = FEventIds<Traits>::template GetId<EventType>();
        if (!Handlers.Contains(EventId)) { return; }

        Events.Emplace(Handlers[EventId]->CreateEvent(TickInfo, &NewEvent));
    }

    template <typename Traits>
    void USimEvents<Traits>::ExecuteEvents(Chaos::FReal ResultsTime, Chaos::FReal SimProxyOffset, ENetRole SimRole) {
        for (const TUniquePtr<FEventWrapperBase>& Event : Events) {
            Event->ExecuteIfNeeded(ResultsTime, SimProxyOffset, SimRole);
        }
    }
}
