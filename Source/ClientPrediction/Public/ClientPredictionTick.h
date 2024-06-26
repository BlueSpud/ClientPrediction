﻿#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    struct FNetTickInfo {
        int32 LocalTick = INDEX_NONE;
        int32 ServerTick = INDEX_NONE;

        bool bHasNetConnection = false;
        bool bIsResim = false;

        Chaos::FReal Dt = 0.0;
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
        struct FSimProxyWorldManager* SimProxyWorldManager = nullptr;
        ENetRole SimRole = ROLE_None;
    };

    struct FSimTickInfo {
        FSimTickInfo(const FNetTickInfo& Info) : LocalTick(Info.LocalTick), Dt(Info.Dt), UpdatedComponent(Info.UpdatedComponent) {}

        int32 LocalTick = INDEX_NONE;
        Chaos::FReal Dt = 0.0;

        class UPrimitiveComponent* UpdatedComponent = nullptr;
    };
}
