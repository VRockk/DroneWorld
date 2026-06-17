using UnrealBuildTool;

public class DroneWorld : ModuleRules
{
	public DroneWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"HeadMountedDisplay",   // HMD detection (UHeadMountedDisplayFunctionLibrary) and VR view setup
			"AIModule", "GameplayTasks", "NavigationSystem",   // Behavior Tree driving the AI drone
			"UMG"   // OSD overlay drawn into the Feed render target
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
