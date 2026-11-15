// R26 internet co-op: the credentials parser never needs values to report what is missing, join codes round-trip and
// untrusted input is refused, every message the flow shows is data, and the gate itself proves the hard rule: the EOS
// transport module is not loaded and GameNetDriver stays the IP driver unless a player picked online.
#include "DCOnline.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Rules/DCOnlineRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOnlineCredentialsTest, "Epic_AI_Mega_Jam2.Online.Credentials", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOnlineCredentialsTest::RunTest(const FString&)
{
	const FString Full = TEXT("; comment\n[Section]\nProductId=p1\nSandboxId = s1\nDeploymentId=\"d1\"\nClientId=c1\r\nClientSecret=x1\n# tail");
	FDCOnlineCredentials C;
	FString Missing;
	TestTrue(TEXT("full file reads"), DCOnlineRules::ReadCredentials(DCOnlineRules::ParseKeyValues(Full), C, Missing));
	TestEqual(TEXT("trimmed"), C.SandboxId, FString(TEXT("s1")));
	TestEqual(TEXT("quotes dropped"), C.DeploymentId, FString(TEXT("d1")));
	TestEqual(TEXT("crlf ok"), C.ClientId, FString(TEXT("c1")));
	TestTrue(TEXT("nothing missing"), Missing.IsEmpty());

	// The shipped template has empty values: every key reported missing by name, no value echoed.
	const FString Template = TEXT("ProductId=\nSandboxId=\nDeploymentId=\nClientId=\nClientSecret=\n");
	TestFalse(TEXT("template refused"), DCOnlineRules::ReadCredentials(DCOnlineRules::ParseKeyValues(Template), C, Missing));
	TestEqual(TEXT("names the keys"), Missing, FString(TEXT("productid, sandboxid, deploymentid, clientid, clientsecret")));

	const FString NoSecret = TEXT("ProductId=p\nSandboxId=s\nDeploymentId=d\nClientId=c\n");
	TestFalse(TEXT("secret missing refused"), DCOnlineRules::ReadCredentials(DCOnlineRules::ParseKeyValues(NoSecret), C, Missing));
	TestEqual(TEXT("only the secret named"), Missing, FString(TEXT("clientsecret")));
	TestFalse(TEXT("an empty file is refused"), DCOnlineRules::ReadCredentials(DCOnlineRules::ParseKeyValues(FString()), C, Missing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOnlineCodeTest, "Epic_AI_Mega_Jam2.Online.JoinCode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOnlineCodeTest::RunTest(const FString&)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	TestTrue(TEXT("code length is short and real"), S.CodeLength >= 4 && S.CodeLength <= 8);
	TestTrue(TEXT("alphabet ships"), S.CodeAlphabet.Len() >= 20);
	for (const TCHAR Confusable : FString(TEXT("O0I1")))
	{
		int32 Unused;
		TestFalse(FString::Printf(TEXT("alphabet avoids %c"), Confusable), S.CodeAlphabet.FindChar(Confusable, Unused));
	}
	FRandomStream Stream(42);
	TSet<FString> Seen;
	for (int32 i = 0; i < 200; ++i)
	{
		const FString Code = DCOnlineRules::MakeCode(S.CodeAlphabet, S.CodeLength, Stream);
		FString Back;
		TestTrue(FString::Printf(TEXT("%s normalizes"), *Code), DCOnlineRules::NormalizeCode(Code, S.CodeAlphabet, S.CodeLength, Back) && Back == Code);
		FString Typed = Code.ToLower();
		Typed.InsertAt(3, TEXT(" - "));
		TestTrue(TEXT("typed in lower case with a dash"), DCOnlineRules::NormalizeCode(Typed, S.CodeAlphabet, S.CodeLength, Back) && Back == Code);
		Seen.Add(Code);
	}
	TestTrue(TEXT("codes vary"), Seen.Num() > 190);
	FString Out;
	TestFalse(TEXT("too short"), DCOnlineRules::NormalizeCode(TEXT("ABC"), S.CodeAlphabet, S.CodeLength, Out));
	TestFalse(TEXT("too long"), DCOnlineRules::NormalizeCode(TEXT("ABCDEFGHJK"), S.CodeAlphabet, S.CodeLength, Out));
	TestFalse(TEXT("foreign characters"), DCOnlineRules::NormalizeCode(TEXT("AB?D;F"), S.CodeAlphabet, S.CodeLength, Out));
	TestFalse(TEXT("a url is not a code"), DCOnlineRules::NormalizeCode(TEXT("EOS:abc"), S.CodeAlphabet, S.CodeLength, Out));
	TestTrue(TEXT("refusal leaves no partial code"), Out.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOnlineSwitchTest, "Epic_AI_Mega_Jam2.Online.Switch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOnlineSwitchTest::RunTest(const FString&)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	// Every message id the flow can show is a data row.
	for (const TCHAR* Id : {TEXT("Off"), TEXT("NoCredentials"), TEXT("LoginFailed"), TEXT("Timeout"), TEXT("ListenFailed"), TEXT("AdvertiseFailed"), TEXT("NotFound"), TEXT("ConnectFailed"), TEXT("BadCode"), TEXT("LoggingIn"), TEXT("Opening"), TEXT("Advertising"), TEXT("Searching"), TEXT("Connecting"), TEXT("Hosting")})
	{
		TestTrue(FString::Printf(TEXT("message %s ships"), Id), S.Messages.Contains(Id));
	}
	TestTrue(TEXT("hosting shows the code"), S.Message(TEXT("Hosting"), TEXT("ABC234")).Contains(TEXT("ABC234")));
	TestFalse(TEXT("credentials file stays under Custom/ (never staged)"), !S.CredentialsFile.StartsWith(TEXT("Custom/")));
	// The hard rule: the gate never loads the transport, and the game net driver is the IP driver.
	TestFalse(TEXT("EOS transport module not loaded"), FModuleManager::Get().IsModuleLoaded(*S.TransportModule));
	bool bFound = false;
	for (const FNetDriverDefinition& Def : GEngine->NetDriverDefinitions)
	{
		if (Def.DefName == NAME_GameNetDriver)
		{
			bFound = true;
			TestFalse(TEXT("GameNetDriver is not the EOS driver"), Def.DriverClassName.ToString().Contains(TEXT("EOS")));
		}
	}
	TestTrue(TEXT("GameNetDriver defined"), bFound);
	TestFalse(TEXT("transport states"), DCOnlineRules::UsesTransport(EDCOnlineState::Off) || DCOnlineRules::UsesTransport(EDCOnlineState::Failed) || DCOnlineRules::UsesTransport(EDCOnlineState::Ready));
	TestTrue(TEXT("hosting carries"), DCOnlineRules::UsesTransport(EDCOnlineState::Hosting) && DCOnlineRules::UsesTransport(EDCOnlineState::Joining));
	return true;
}

#endif
