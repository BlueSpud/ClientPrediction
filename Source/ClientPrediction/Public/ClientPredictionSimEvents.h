#pragma once

#include "CoreMinimal.h"

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

    struct FEventHandlerBase {
        virtual ~FEventHandlerBase() = default;
    };

    template <typename EventType>
    struct FEventHandler : public FEventHandlerBase {
        TMulticastDelegate<void(const EventType&)> Delegate;
    };

    template <typename Traits>
    struct FSimEvents {
        template <typename EventType>
        TMulticastDelegate<void(const EventType&)>& RegisterEvent();

        template <typename Event>
        void DispatchEvent(const Event& NewEvent);

    private:
        TMap<EventId, TUniquePtr<FEventHandlerBase>> Handlers;
    };

    template <typename Traits>
    template <typename EventType>
    TMulticastDelegate<void(const EventType&)>& FSimEvents<Traits>::RegisterEvent() {
        const EventId EventId = FEventIds<Traits>::template GetId<EventType>();
        TUniquePtr<FEventHandler<EventType>> Handler = MakeUnique<FEventHandler<EventType>>();

        TMulticastDelegate<void(const EventType&)> Delegate = Handler->Delegate;
        Handlers.Add(EventId, MoveTemp(Handler));

        return Delegate;
    }

    template <typename Traits>
    template <typename Event>
    void FSimEvents<Traits>::DispatchEvent(const Event& NewEvent) {
        // Serialize

        // Inject to factory
    }

    //
    // template <typename... Types>
    // struct FEvents {
    //     static void Register() {
    //         RegisterEvent<Types>();
    //     }
    // };
}
