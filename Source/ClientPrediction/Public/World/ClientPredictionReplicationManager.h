#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClientPredictionReplicationManager.generated.h"

UCLASS()
class CLIENTPREDICTION_API AClientPredictionReplicationManager : public AActor {
	GENERATED_BODY()

public:
	AClientPredictionReplicationManager();

	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	virtual void PostNetInit() override;
};
