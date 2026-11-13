using UnrealBuildTool;

public class Epic_AI_Mega_Jam2 : ModuleRules
{
	public Epic_AI_Mega_Jam2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings", "PhysicsCore", "ProceduralMeshComponent" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG", "AudioMixer", "ImageWrapper", "Sockets", "RenderCore" });
	}
}
