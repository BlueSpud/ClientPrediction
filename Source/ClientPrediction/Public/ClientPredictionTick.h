#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    struct FNetTickInfo {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        bool bHasNetConnection = false;
        bool bIsResim = false;

        Chaos::FReal Dt = 0.0;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        ENetRole SimRole = ROLE_None;
    };
}
