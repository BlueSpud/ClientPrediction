#pragma once

#include "CoreMinimal.h"

namespace ClientPrediction {
    enum CLIENTPREDICTION_API EDataCompleteness : uint8 {
        kLow = 0,
        kFull,
        kCount
    };
}
