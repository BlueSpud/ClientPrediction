#include "Driver/Drivers/ClientPredictionModelAutoProxyDriver.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API int32 ClientPredictionInputSlidingWindowSize = 3;
    FAutoConsoleVariableRef CVarClientPredictionSlidingWindowSize(TEXT("cp.SlidingWindowSize"), ClientPredictionInputSlidingWindowSize,
                                                                  TEXT("The max size of the sliding window of inputs that is sent to the authority"));

    CLIENTPREDICTION_API float ClientPredictionMaxTimeDilation = 0.01;
    FAutoConsoleVariableRef CVarClientPredictionMaxTimeDilation(TEXT("cp.MaxTimeDilation"), ClientPredictionMaxTimeDilation,
                                                                TEXT("The maximum time dilation used by the auto proxies"));
}
