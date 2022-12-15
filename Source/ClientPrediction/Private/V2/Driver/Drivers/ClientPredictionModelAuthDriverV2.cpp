#include "V2/Driver/Drivers/ClientPredictionModelAuthDriverV2.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API int32 ClientPredictionDesiredInputBufferSize = 3;
	FAutoConsoleVariableRef CVarClientPredictionDesiredInputBufferSize(TEXT("cp.DesiredInputBufferSize"), ClientPredictionDesiredInputBufferSize, TEXT("The desired size of the input buffer on the authority"));
}
