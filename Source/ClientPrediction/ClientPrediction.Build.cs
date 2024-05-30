// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClientPrediction : ModuleRules
{
	public ClientPrediction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Chaos", "PhysicsCore", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "ChaosCore", "NetCore" });
	}
}