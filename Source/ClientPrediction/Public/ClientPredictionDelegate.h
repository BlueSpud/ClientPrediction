#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    template <typename Traits>
    struct FSimDelegates {
        DECLARE_MULTICAST_DELEGATE_ThreeParams(FInputDelegate, typename Traits::InputType& Input, Chaos::FReal Dt, int32 TickNumber);
        FInputDelegate ProduceInputGameThread; // Called on game thread
        FInputDelegate ProduceInputPhysicsThread; // Called on physics thread
    };
}
