// Building HP (human 16:32): a standard strike takes 35-50 % of a mid tower, so 2-3 strikes fell it; Bunker Buster and
// Sun-Drop one-shot a small tower; damage falls off to the blast edge and is zero beyond it. Shipped numbers from data.
#include "Misc/AutomationTest.h"
#include "DCTuning.h"
#include "Rules/DCTowerHealth.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCTowerHealthTest, "Epic_AI_Mega_Jam2.Voxel.TowerHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCTowerHealthTest::RunTest(const FString&)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const UDCTuning& T = UDCTuning::Get();
	auto Strike = [&T](const TCHAR* Id) { const FDCStratagemDef* D = T.FindStratagem(Id); return D ? D->Damage : 0.f; };
	const float Mid = DCTowerHealth::Health(10, 3, 3, Tn.TowerHealthPerFloor, Tn.TowerHealthMin, Tn.TowerHealthMax);
	const float Small = DCTowerHealth::Health(5, 2, 2, Tn.TowerHealthPerFloor, Tn.TowerHealthMin, Tn.TowerHealthMax);
	const float Laser = DCTowerHealth::Damage(Strike(TEXT("OrbitalLaser")), 0.f, 1300.f, Tn.TowerDamageEdge);
	TestTrue(*FString::Printf(TEXT("a standard strike takes 35-50 %% of a mid tower (%.0f of %.0f)"), Laser, Mid), Laser >= 0.35f * Mid && Laser <= 0.5f * Mid);
	TestTrue(TEXT("one standard strike never fells a small tower"), Laser < Small);
	TestTrue(TEXT("Bunker Buster one-shots a small tower"), DCTowerHealth::Damage(Strike(TEXT("BunkerBuster")), 0.f, 1500.f, Tn.TowerDamageEdge) >= Small);
	TestTrue(TEXT("Sun-Drop one-shots a small tower"), DCTowerHealth::Damage(Strike(TEXT("SunDrop")), 0.f, 2600.f, Tn.TowerDamageEdge) >= Small);
	TestEqual(TEXT("edge of the blast deals EdgeScale"), DCTowerHealth::Damage(100.f, 1000.f, 1000.f, 0.6f), 60.f, 0.01f);
	TestEqual(TEXT("beyond the blast deals nothing"), DCTowerHealth::Damage(100.f, 1001.f, 1000.f, 0.6f), 0.f);
	TestTrue(TEXT("health stays inside [Min, Max]"), DCTowerHealth::Health(40, 6, 6, 110.f, 550.f, 2000.f) <= 2000.f && DCTowerHealth::Health(1, 1, 1, 110.f, 550.f, 2000.f) >= 550.f);
	return true;
}

#endif
