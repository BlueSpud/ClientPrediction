#pragma once

#include "CoreMinimal.h"

class AClientPredictionSimProxyManager;

namespace ClientPrediction {
    struct FTickInfo {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        Chaos::FReal Dt = 0.0;
        bool bIsResim = false;
    };

    struct FNetTickInfo : public FTickInfo {
        bool bHasNetConnection = false;

        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        AClientPredictionSimProxyManager* SimProxyWorldManager = nullptr;
        ENetRole SimRole = ROLE_None;
    };

    struct FSimTickInfo {
        FSimTickInfo(const FNetTickInfo& Info) : LocalTick(Info.LocalTick), Dt(Info.Dt), UpdatedComponent(Info.UpdatedComponent) {}

        int32 LocalTick = INDEX_NONE;
        Chaos::FReal Dt = 0.0;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
    };
}
