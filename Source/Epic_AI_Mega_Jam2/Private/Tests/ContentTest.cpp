// R8 / R12 content rows: every weapon names a forged model and a sound that renders, with sane stats; every data
// sound recipe renders audible PCM; every stratagem has a carve row.
#include "Misc/AutomationTest.h"
#include "DCAudio.h"
#include "DCTuning.h"
#include "Rules/DCStrikePattern.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCWeaponRowsTest, "Epic_AI_Mega_Jam2.Content.Weapons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCWeaponRowsTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	TestTrue(TEXT("R8: more weapons than the rifle"), T.Weapons.Num() >= 4);
	const TArray<FName> Cues = DCAudio::CueNames();
	TSet<FName> Ids;
	for (const FDCWeaponDef& W : T.Weapons)
	{
		const FString Id = W.Id.ToString();
		bool bDuplicate = false;
		Ids.Add(W.Id, &bDuplicate);
		TestFalse(*FString::Printf(TEXT("%s id is unique"), *Id), bDuplicate);
		TestNotNull(*FString::Printf(TEXT("%s model is forged"), *Id), DCVoxelModels::Parsed(W.Model));
		TestTrue(*FString::Printf(TEXT("%s sound is a cue"), *Id), Cues.Contains(W.Sound));
		TestTrue(*FString::Printf(TEXT("%s sound renders"), *Id), DCAudio::Render(W.Sound).Num() > 0);
		TestTrue(*FString::Printf(TEXT("%s stats are positive"), *Id), W.Stats.MagazineSize > 0 && W.Stats.ShotsPerSecond > 0.f
			&& W.Stats.Damage > 0.f && W.Stats.RangeCm > 0.f && W.Stats.ReloadSeconds > 0.f && W.Pellets >= 1);
		TestFalse(*FString::Printf(TEXT("%s has a HUD label"), *Id), W.HudLabel.IsEmpty());
	}
	TestEqual(TEXT("weapon index fits the replicated byte"), T.NumWeapons() <= 255, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCSoundRecipeRowsTest, "Epic_AI_Mega_Jam2.Content.SoundRecipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCSoundRecipeRowsTest::RunTest(const FString&)
{
	for (const FDCSoundRecipe& R : UDCTuning::Get().SoundRecipes)
	{
		const TArray<int16> Pcm = DCAudio::Render(R.Id);
		int32 Peak = 0;
		for (const int16 Sample : Pcm)
		{
			Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
		}
		TestTrue(*FString::Printf(TEXT("%s renders audible PCM"), *R.Id.ToString()), Pcm.Num() > 0 && Peak > 2000);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCStrikeRowsTest, "Epic_AI_Mega_Jam2.Content.Strikes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCStrikeRowsTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	const TArray<FName> Cues = DCAudio::CueNames();
	const TArray<FDCVoxelCarveDef>& Carves = UDCVoxelSettings::Get().Carves;
	for (const FDCStratagemDef& D : T.Stratagems)
	{
		const FString Id = D.Id.ToString();
		for (const FName Cue : {D.ChargeSound, D.BeepSound, D.BlastSound})
		{
			TestTrue(*FString::Printf(TEXT("%s sound %s is a cue"), *Id, *Cue.ToString()), Cues.Contains(Cue));
		}
		TestTrue(*FString::Printf(TEXT("%s pattern is registered"), *Id), DCStrikePattern::Known(D.Pattern));
		TestTrue(*FString::Printf(TEXT("%s has its own carve row"), *Id), Carves.ContainsByPredicate([&D](const FDCVoxelCarveDef& C) { return C.Stratagem == D.Id; }));
	}
	// R12: the draft offers more strike types than the first run's three.
	int32 DraftStrikes = 0;
	for (const FDCUpgradeDef& U : T.Upgrades)
	{
		DraftStrikes += U.Effect == TEXT("UnlockStratagem") && T.FindStratagem(U.Unlock) ? 1 : 0;
	}
	TestTrue(TEXT("R12: six or more strike unlocks in the draft"), DraftStrikes >= 6);
	// The line pattern spans the whole pass, centred on the target.
	FRandomStream Rng(1);
	const FVector2D First = DCStrikePattern::Offset(TEXT("Line"), {0, 5, 1000.f, FVector2D(1.f, 0.f)}, Rng);
	const FVector2D Last = DCStrikePattern::Offset(TEXT("Line"), {4, 5, 1000.f, FVector2D(1.f, 0.f)}, Rng);
	TestTrue(TEXT("line pattern runs -span/2 .. +span/2"), First.Equals(FVector2D(-500.f, 0.f), 1.f) && Last.Equals(FVector2D(500.f, 0.f), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCSkyPropRowsTest, "Epic_AI_Mega_Jam2.Content.SkyProps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCSkyPropRowsTest::RunTest(const FString&)
{
	const TArray<FDCSkyPropDef>& Rows = UDCTuning::Get().SkyProps;
	TestTrue(TEXT("R12: sky detail and skyline rows are shipped"), Rows.Num() > 0);
	int32 Total = 0;
	for (const FDCSkyPropDef& R : Rows)
	{
		const FString Id = R.Model.ToString();
		TestNotNull(*FString::Printf(TEXT("%s is a forged model"), *Id), DCVoxelModels::Parsed(R.Model));
		TestTrue(*FString::Printf(TEXT("%s ring and scale are ordered"), *Id), R.RingCm.X <= R.RingCm.Y && R.Scale.X > 0.f && R.Scale.X <= R.Scale.Y);
		Total += R.Count;
	}
	TestTrue(TEXT("a handful of sky meshes, not hundreds (60 fps)"), Total <= 40);
	return true;
}

#endif
