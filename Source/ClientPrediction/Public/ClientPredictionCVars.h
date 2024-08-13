#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API float ClientPredictionPositionTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionVelocityTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionRotationTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance;

    extern CLIENTPREDICTION_API int32 ClientPredictionAutoProxySendInterval;

    extern CLIENTPREDICTION_API int32 ClientPredictionSimProxySendInterval;
    extern CLIENTPREDICTION_API int32 ClientPredictionSimProxyBufferTicks;
    extern CLIENTPREDICTION_API int32 ClientPredictionSimProxyCorrectionThreshold;

    extern CLIENTPREDICTION_API int32 ClientPredictionInputWindowSize;

    extern CLIENTPREDICTION_API float ClientPredictionSimProxyTickInterval;
}
