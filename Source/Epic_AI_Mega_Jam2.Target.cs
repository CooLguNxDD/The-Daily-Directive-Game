using UnrealBuildTool;
using System.Collections.Generic;

public class Epic_AI_Mega_Jam2Target : TargetRules
{
	public Epic_AI_Mega_Jam2Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.Add("Epic_AI_Mega_Jam2");
	}
}
