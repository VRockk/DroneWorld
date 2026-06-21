// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class NNETensorRT_RTX : ModuleRules
{
	public NNETensorRT_RTX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDependencyModuleNames.Add("CUDAHeader");

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Internal"));

		string PlatformRelativePath = Path.Combine("Binaries", "ThirdParty", "TensorRT_RTX");
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string TensorRTName = "tensorrt_rtx_1_3";
			string OnnxParserName = "tensorrt_onnxparser_rtx_1_3";
			string TensorRTSharedLibName = TensorRTName + ".dll";
			string OnnxParserSharedLibName = OnnxParserName + ".dll";

			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Win64", "tensorrt_rtx_1_3.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Win64", "tensorrt_onnxparser_rtx_1_3.lib"));

			RuntimeDependencies.Add(Path.Combine(PluginDirectory, PlatformRelativePath, "Win64", TensorRTSharedLibName));
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, PlatformRelativePath, "Win64", OnnxParserSharedLibName));

			PublicDelayLoadDLLs.Add(TensorRTSharedLibName);
			PublicDelayLoadDLLs.Add(OnnxParserSharedLibName);

			PublicDefinitions.Add("TENSORRT_RTX_TENSORRT_SHAREDLIB_PATH=" + Path.Combine(PlatformRelativePath, "Win64", TensorRTSharedLibName).Replace('\\', '/'));
			PublicDefinitions.Add("TENSORRT_RTX_ONNXPARSER_SHAREDLIB_PATH=" + Path.Combine(PlatformRelativePath, "Win64", OnnxParserSharedLibName).Replace('\\', '/'));
		}
	}
}
