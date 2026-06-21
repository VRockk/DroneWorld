using System;
using System.IO;
using UnrealBuildTool;

public class NNERuntimeTRT : ModuleRules
{
	public NNERuntimeTRT(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] 
		{ 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore",
			"RenderCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"NNE",
			"RHI",
			"Projects",
			"TraceLog",
			"NNETensorRT_RTX",
			"CUDA"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"D3D12RHI"
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[]
			{
				"DirectML",
				"DX12"
			});

			PublicDefinitions.Add("WITH_NNE_RUNTIME_TRT");
			PrivateDefinitions.Add("ENABLE_FEATURE_DISABLE_RUNTIME_ALLOCATION=0");
		}
	}
}
