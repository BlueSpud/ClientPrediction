#include "Driver/Drivers/ClientPredictionModelSimProxyDriver.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API float ClientPredictionSimProxyDelay = 0.050;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyDelay(TEXT("cp.SimProxyDelay"), ClientPredictionSimProxyDelay,
                                                              TEXT("The target delay from the last authority state recieved from the authority for sim proxies"));

    CLIENTPREDICTION_API float ClientPredictionSimProxyTimeDilationMargin = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyTimeDilationMargin(TEXT("cp.SimProxyTimeDilationMargin"), ClientPredictionSimProxyTimeDilationMargin,
                                                                           TEXT(
                                                                               "If the time in the sim proxy buffer is this percentage off from SimProxyDelay, time will be dilated for that sim proxy"));

    CLIENTPREDICTION_API float ClientPredictionSimProxyAggressiveTimeDilationMargin = 0.8;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyAggressiveTimeDilationMargin(TEXT("cp.SimProxyAggressiveTimeDilationMargin"), ClientPredictionSimProxyAggressiveTimeDilationMargin,
                                                                           TEXT(
                                                                               "If the time in the sim proxy buffer is this percentage off from SimProxyDelay, time will be aggressively dilated for that sim proxy"));

    CLIENTPREDICTION_API float ClientPredictionSimProxyTimeDilation = 0.02;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyTimeDilation(TEXT("cp.SimProxyTimeDilation"), ClientPredictionSimProxyTimeDilation,
                                                                     TEXT("The time dilation for a sim proxy when time is being dilated"));

    CLIENTPREDICTION_API float ClientPredictionSimProxyAggressiveTimeDilation = 0.2;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyAggressiveTimeDilation(TEXT("cp.SimProxyAggressiveTimeDilation"), ClientPredictionSimProxyAggressiveTimeDilation,
                                                                               TEXT("The time dilation for a sim proxy when time is being dilated aggressively"));
}
