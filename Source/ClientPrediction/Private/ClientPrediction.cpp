#include "ClientPrediction.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif

#include "ClientPredictionSettings.h"
#include "GameFramework/GameModeBase.h"
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
	const UWorld* World = PlayerController->GetWorld();
	const ENetMode NetMode = World->GetNetMode();

	if (NetMode != ENetMode::NM_DedicatedServer && NetMode != ENetMode::NM_ListenServer) {
		return;
	}

	ClientPrediction::FWorldManager* WorldManager = ClientPrediction::FWorldManager::ManagerForWorld(World);
	if (WorldManager != nullptr) {
		WorldManager->CreateReplicationManagerForPlayer(PlayerController);
	}
}

void FClientPredictionModule::OnPlayerLogout(AGameModeBase* Gamemode, AController* PlayerController) {
	const UWorld* World = Gamemode->GetWorld();
	const ENetMode NetMode = World->GetNetMode();

	if (NetMode != ENetMode::NM_DedicatedServer && NetMode != ENetMode::NM_ListenServer) {
		return;
	}

	ClientPrediction::FWorldManager* WorldManager = ClientPrediction::FWorldManager::ManagerForWorld(World);
	if (WorldManager != nullptr) {
		WorldManager->DestroyReplicationManagerForPlayer(PlayerController);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClientPredictionModule, ClientPrediction)
