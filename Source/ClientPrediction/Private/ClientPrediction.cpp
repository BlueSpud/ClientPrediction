#include "ClientPrediction.h"

#include "ClientPredictionSimProxy.h"

#define LOCTEXT_NAMESPACE "FClientPredictionModule"

CLIENTPREDICTION_API DEFINE_LOG_CATEGORY(LogClientPrediction);

FDelegateHandle FClientPredictionModule::OnPostWorldInitializationDelegate;
FDelegateHandle FClientPredictionModule::OnWorldCleanupDelegate;

void FClientPredictionModule::StartupModule() {
	OnPostWorldInitializationDelegate = FWorldDelegates::OnPostWorldInitialization.AddStatic(&OnPostWorldInitialize);
	OnWorldCleanupDelegate = FWorldDelegates::OnWorldCleanup.AddStatic(&OnWorldCleanup);
}

void FClientPredictionModule::ShutdownModule() {
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationDelegate);
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupDelegate);
}

void FClientPredictionModule::OnPostWorldInitialize(UWorld* InWorld, const UWorld::InitializationValues) {
	AClientPredictionSimProxyManager::InitializeWorld(InWorld);
}

void FClientPredictionModule::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources) {
	AClientPredictionSimProxyManager::CleanupWorld(InWorld);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClientPredictionModule, ClientPrediction)
