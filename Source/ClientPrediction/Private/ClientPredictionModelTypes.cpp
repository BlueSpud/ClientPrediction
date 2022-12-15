#include "ClientPredictionModelTypes.h"

namespace ClientPrediction {
	CLIENTPREDICTION_API float ClientPredictionPositionTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionPositionTolerance(TEXT("cp.PositionTolerance"), ClientPredictionPositionTolerance, TEXT("If the position deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionVelocityTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionVelocityTolerance(TEXT("cp.VelocityTolerance"), ClientPredictionVelocityTolerance, TEXT("If the velcoity deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionRotationTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionRotationTolerance(TEXT("cp.RotationTolerance"), ClientPredictionRotationTolerance, TEXT("If the rotation deleta is less than this, a correction won't be applied"));

	CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance = 0.1;
	FAutoConsoleVariableRef CVarClientPredictionAngularVelTolerance(TEXT("cp.AngularVelTolerance"), ClientPredictionAngularVelTolerance, TEXT("If the angular velocity deleta is less than this, a correction won't be applied"));
}
