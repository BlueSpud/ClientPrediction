#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
	enum CLIENTPREDICTION_API ERelevancy : uint8 {
		kLowRelevancy = 0,
		kRelevant,
		kAutoProxy,
		kCount
	};
}