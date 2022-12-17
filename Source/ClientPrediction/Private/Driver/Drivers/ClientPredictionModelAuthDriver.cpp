#include "Driver/Drivers/ClientPredictionModelAuthDriver.h"

namespace ClientPrediction {
    CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize = 3;
    FAutoConsoleVariableRef CVarClientPredictionDesiredInputBufferSize(
        TEXT("cp.DesiredInputBufferSize"), ClientPredictionDesiredInputBufferSize, TEXT("The desired size of the input buffer on the authority"));

    CLIENTPREDICTION_API float ClientPredictionTimeDilationAlpha = 0.1;
    FAutoConsoleVariableRef CVarClientPredictionTimeDilationAlpha(TEXT("cp.TimeDilationalpha"), ClientPredictionTimeDilationAlpha,
                                                                TEXT("The alpha used to lerp towards the target time dilation"));
}
