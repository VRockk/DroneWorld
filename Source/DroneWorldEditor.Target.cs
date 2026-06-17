using UnrealBuildTool;
using System.Collections.Generic;

public class DroneWorldEditorTarget : TargetRules
{
	public DroneWorldEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("DroneWorld");
	}
}
