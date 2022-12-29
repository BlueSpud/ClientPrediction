#include "Driver/Drivers/ClientPredictionModelAutoProxyDriver.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
    FAutoConsoleVariableRef CVarClientPredictionSlidingWindowSize(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize,
                                                                  TEXT("The max size of the sliding window of inputs that is sent to the authority"));

    CLIENTPREDICTION_API float ClientPredictionMaxTimeDilation = 0.01;
    FAutoConsoleVariableRef CVarClientPredictionMaxTimeDilation(TEXT("cp.MaxTimeDilation"), ClientPredictionMaxTimeDilation,
                                                                TEXT("The maximum time dilation used by the auto proxies"));

    CLIENTPREDICTION_API float ClientPredictionAuthorityCatchupTimescale = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionAuthorityCatchupTimescale(TEXT("cp.ClientPredictionAuthorityCatchupTimescale"), ClientPredictionAuthorityCatchupTimescale,
                                                                TEXT("If the authority ever gets too far behind the auto proxy, this is the timescale that will be used for the auto proxy to allow the authority to catch back up"));
}
