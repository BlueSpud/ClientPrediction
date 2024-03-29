﻿#pragma once

#include "CoreMinimal.h"

#include "ClientPredictionSettings.generated.h"

UCLASS(config=ClientPrediction, defaultconfig, meta=(DisplayName="ClientPrediction"))
class CLIENTPREDICTION_API UClientPredictionSettings : public UObject {
    GENERATED_BODY()

public:
    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float FixedDt = 1.0 / 60.0;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float MaxPhysicsTime = 1.0;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 HistoryTimeMs = 500.0;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 MaxForcedSimulationTicks = 50;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 DesiredInputBufferSize = 3;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float UnhealthyInputBufferPercentage = 1.2;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 DroppedPacketMemoryTickLength = 25;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 InputSlidingWindowSize = 3;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float TimeDilationAlpha = 0.1;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float MaxAutoProxyTimeDilation = 0.01;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float AutoProxyMaxSpeedupTimescale = 1.4;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float AutoProxyAuthorityCatchupTimescale = 0.1;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float SimProxyDelay = 0.100;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float SimProxyAggressiveTimeDifference = 0.200;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    float SimProxySnapTimeDifference = 0.400;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    double SimProxyTimeDilation = 0.01;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    double SimProxyTimeDilationAlpha = 0.1;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    int32 SnapshotSendCadence = 3;

    UPROPERTY(config, EditAnywhere, Category = "ClientPrediction", meta=(ShowOnlyInnerProperties))
    bool bDisableCollisionsOnSimProxies = true;
};
