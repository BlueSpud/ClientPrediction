#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogClientPrediction, Log, All);

class FClientPredictionModule : public IModuleInterface {
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    static FDelegateHandle OnPostWorldInitializationDelegate;
    static FDelegateHandle OnWorldCleanupDelegate;

    static void OnPostWorldInitialize(UWorld* InWorld, const UWorld::InitializationValues);
    static void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

    static FDelegateHandle OnPlayerLoginDelegate;
    static FDelegateHandle OnPlayerLogoutDelegate;

    static void OnPlayerLogin(AGameModeBase* GameMode, APlayerController* PlayerController);
    static void OnPlayerLogout(class AGameModeBase* GameMode, class AController* PlayerController);
};
