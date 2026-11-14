// R25 prop light rows: every row names a forged voxel model, and no prop light uses the player's signal yellow
// (art bible: lime-yellow only for what the player can use) or danger red.
#include "Misc/AutomationTest.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCPropLightRowsTest, "Epic_AI_Mega_Jam2.Look.PropLights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCPropLightRowsTest::RunTest(const FString&)
{
	const TArray<FDCPropLightDef>& Rows = UDCVoxelSettings::Get().PropLights;
	TestTrue(TEXT("prop light rows are shipped"), Rows.Num() > 0);
	for (const FDCPropLightDef& R : Rows)
	{
		TestNotNull(*FString::Printf(TEXT("%s is a forged model"), *R.Model.ToString()), DCVoxelModels::Parsed(R.Model));
		const FLinearColor H = R.Color.LinearRGBToHSV();
		const bool bSignal = H.R > 55.f && H.R < 90.f && H.G > 0.5f;
		const bool bDanger = (H.R < 8.f || H.R > 352.f) && H.G > 0.8f;
		TestFalse(*FString::Printf(TEXT("%s light is not signal yellow"), *R.Model.ToString()), bSignal);
		TestFalse(*FString::Printf(TEXT("%s light is not danger red"), *R.Model.ToString()), bDanger);
		TestTrue(*FString::Printf(TEXT("%s light has reach"), *R.Model.ToString()), R.Candela > 0.f && R.RadiusCm > 0.f);
	}
	return true;
}

#endif
