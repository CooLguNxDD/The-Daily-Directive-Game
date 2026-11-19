using UnrealBuildTool;

// R26 internet co-op transport over EOS P2P. LoadingPhase None in the .uproject: only an online attempt
// (UDCOnline, game module) loads it; solo, LAN and the gate never do. The game module does not link it.
public class DCOnlineEos : ModuleRules
{
	public DCOnlineEos(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Sockets", "NetCore", "OnlineSubsystemUtils", "EOSShared", "EOSSDK", "SocketSubsystemEOS", "Epic_AI_Mega_Jam2" });
	}
}
