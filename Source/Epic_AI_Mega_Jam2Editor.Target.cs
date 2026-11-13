using UnrealBuildTool;
using System.Collections.Generic;

public class Epic_AI_Mega_Jam2EditorTarget : TargetRules
{
	public Epic_AI_Mega_Jam2EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.Add("Epic_AI_Mega_Jam2");
	}
}
