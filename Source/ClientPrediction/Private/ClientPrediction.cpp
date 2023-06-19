#include "ClientPrediction.h"

#include "ISettingsModule.h"

#include "ClientPredictionSettings.h"
#include "GameFramework/GameModeBase.h"
#include "World/ClientPredictionReplicationManager.h"
#include "World/ClientPredictionWorldManager.h"

#define LOCTEXT_NAMESPACE "FClientPredictionModule"

DEFINE_LOG_CATEGORY(LogClientPrediction);

FDelegateHandle FClientPredictionModule::OnPostWorldInitializationDelegate;
FDelegateHandle FClientPredictionModule::OnWorldCleanupDelegate;

FDelegateHandle FClientPredictionModule::OnPlayerLoginDelegate;
FDelegateHandle FClientPredictionModule::OnPlayerLogoutDelegate;

void FClientPredictionModule::StartupModule() {
#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr) {
		SettingsModule->RegisterSettings("Project", "Project", "ClientPrediction",
		                                 LOCTEXT("ClientPredictionSettingsName", "ClientPrediction"),
		                                 LOCTEXT("ClientPredictionSettingsDescription", "Settings for ClientPrediction"),
		                                 GetMutableDefault<UClientPredictionSettings>()
		);
	}
#endif

	OnPostWorldInitializationDelegate = FWorldDelegates::OnPostWorldInitialization.AddStatic(&OnPostWorldInitialize);
	OnWorldCleanupDelegate = FWorldDelegates::OnWorldCleanup.AddStatic(&OnWorldCleanup);

	OnPlayerLoginDelegate = FGameModeEvents::GameModePostLoginEvent.AddStatic(&OnPlayerLogin);
	OnPlayerLogoutDelegate = FGameModeEvents::GameModeLogoutEvent.AddStatic(&OnPlayerLogout);
}

void FClientPredictionModule::ShutdownModule() {
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationDelegate);
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupDelegate);

	FGameModeEvents::GameModePostLoginEvent.Remove(OnPlayerLoginDelegate);
	FGameModeEvents::GameModeLogoutEvent.Remove(OnPlayerLogoutDelegate);
}

void FClientPredictionModule::OnPostWorldInitialize(UWorld* InWorld, const UWorld::InitializationValues) {
	ClientPrediction::FWorldManager::InitializeWorld(InWorld);
}

void FClientPredictionModule::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources) {
	ClientPrediction::FWorldManager::CleanupWorld(InWorld);
}

void FClientPredictionModule::OnPlayerLogin(AGameModeBase* /* Gamemode */, APlayerController* PlayerController) {
	if (PlayerController->GetRemoteRole() == ENetRole::ROLE_Authority) { return; }

	ClientPrediction::FWorldManager* WorldManager = ClientPrediction::FWorldManager::ManagerForWorld(PlayerController->GetWorld());
	if (WorldManager != nullptr) {
		WorldManager->CreateReplicationManagerForPlayer(PlayerController);
	}
}

void FClientPredictionModule::OnPlayerLogout(AGameModeBase* /* Gamemode */, AController* PlayerController) {
	if (PlayerController->GetRemoteRole() == ENetRole::ROLE_Authority) { return; }

	ClientPrediction::FWorldManager* WorldManager = ClientPrediction::FWorldManager::ManagerForWorld(PlayerController->GetWorld());
	if (WorldManager != nullptr) {
		WorldManager->DestroyReplicationManagerForPlayer(PlayerController);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClientPredictionModule, ClientPrediction)
