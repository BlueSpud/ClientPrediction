#include "Driver/Drivers/ClientPredictionModelSimProxyDriver.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API float ClientPredictionSimProxyDelay = 0.050;
    FAutoConsoleVariableRef CVarClientPredictionSimProxyDelay(TEXT("cp.SimProxyDelay"), ClientPredictionSimProxyDelay,
                                                              TEXT("The target delay from the last authority state recieved from the authority for sim proxies"));
}
