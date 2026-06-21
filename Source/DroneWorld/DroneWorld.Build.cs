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

		PrivateDependencyModuleNames.AddRange(new string[] {
			"NNE",                       // inference (UNNEModelData, INNERuntimeGPU) via NNERuntimeTRT
			"RenderCore", "RHI", "Renderer",   // SceneViewExtension backbuffer capture (RDG, ScreenPass, GPU readback)
			"Slate", "SlateCore",        // detection box overlay + on-screen config panel
			"MediaAssets",               // video-playback detection level (MediaPlayer/MediaTexture/FileMediaSource)
			"CinematicCamera",           // UCineCameraComponent — the detector's pan/tilt/zoom camera
			"Projects"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
