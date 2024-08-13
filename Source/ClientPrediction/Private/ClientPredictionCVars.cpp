#include "ClientPredictionCVars.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API float ClientPredictionPositionTolerance = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionPositionTolerance(TEXT("cp.PositionTolerance"), ClientPredictionPositionTolerance,
                                                                  TEXT("If the position deleta is less than this, a correction won't be applied"));

    CLIENTPREDICTION_API float ClientPredictionVelocityTolerance = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionVelocityTolerance(TEXT("cp.VelocityTolerance"), ClientPredictionVelocityTolerance,
                                                                  TEXT("If the velcoity deleta is less than this, a correction won't be applied"));

    CLIENTPREDICTION_API float ClientPredictionRotationTolerance = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionRotationTolerance(TEXT("cp.RotationTolerance"), ClientPredictionRotationTolerance,
                                                                  TEXT("If the rotation deleta is less than this, a correction won't be applied"));

    CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionAngularVelTolerance(TEXT("cp.AngularVelTolerance"), ClientPredictionAngularVelTolerance,
                                                                    TEXT("If the angular velocity deleta is less than this, a correction won't be applied"));

    CLIENTPREDICTION_API int32 ClientPredictionAutoProxySendInterval = 4;
    FAutoConsoleVariableRef CVarClientPredictionAutoProxySendInterval(TEXT("cp.AutoProxySendInterval"), ClientPredictionSimProxySendInterval,
                                                                      TEXT("1 out of cp.AutoProxySendInterval ticks will be sent to auto proxies"));

    CLIENTPREDICTION_API int32 ClientPredictionSimProxySendInterval = 2;
    FAutoConsoleVariableRef CVarClientPredictionSimProxySendInterval(TEXT("cp.SimProxySendInterval"), ClientPredictionSimProxySendInterval,
                                                                     TEXT("1 out of cp.SimProxySendInterval ticks will be sent to sim proxies"));

    CLIENTPREDICTION_API int32 ClientPredictionSimProxyBufferTicks = 6;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyBufferTicks(TEXT("cp.SimProxyBufferTicks"), ClientPredictionSimProxyBufferTicks,
                                                                    TEXT("The number of ticks to buffer states recieved from the authority for sim proxies"));

    CLIENTPREDICTION_API int32 ClientPredictionSimProxyCorrectionThreshold = 6;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyCorrectionThreshold(TEXT("cp.SimProxyCorrectionThreshold"), ClientPredictionSimProxyCorrectionThreshold,
                                                                            TEXT(
                                                                                "If the client gets this number of ticks away from the desired sim proxy offset a correction is applied"));

    CLIENTPREDICTION_API int32 ClientPredictionInputWindowSize = 3;
    FAutoConsoleVariableRef CVarClientPredictionInputWindowSize(TEXT("cp.InputWindowSize"), ClientPredictionInputWindowSize,
                                                                TEXT("The size of the sliding window used to send inputs"));

    CLIENTPREDICTION_API float ClientPredictionSimProxyTickInterval = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyTickInterval(TEXT("cp.SimProxyTickInterval"), ClientPredictionSimProxyTickInterval,
                                                                     TEXT("The interval that the authority sends the latest tick to the remotes"));
}
