// Gate tier B needs at least one automation test (tools/gate/verify.sh fails when none ran).
// Game rules get their own tests beside this one: pure C++ that runs under -nullrhi.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpic_AI_Mega_Jam2SmokeTest, "Epic_AI_Mega_Jam2.Smoke.ModuleLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpic_AI_Mega_Jam2SmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the game module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("Epic_AI_Mega_Jam2")));
	return true;
}

#endif
