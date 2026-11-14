// R3 factions: the pure mix roll, hostility and unit picks, and that the shipped faction data resolves (every unit is
// an +Enemies row of that faction, every strike a player-locked +Stratagems row, the army never fights the crew).
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCFactionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCFactionMixTest, "Epic_AI_Mega_Jam2.Faction.Mix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCFactionMixTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	TestTrue(TEXT("factions are shipped"), T.Factions.Num() >= 4);
	TSet<FName> Mains;
	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		for (int32 Sector = 0; Sector < 3; ++Sector)
		{
			const FDCFactionMix Mix = DCFactionRules::RollMix(T.Factions, T.FactionMix, Seed * 7919, Sector);
			const FDCFactionMix Again = DCFactionRules::RollMix(T.Factions, T.FactionMix, Seed * 7919, Sector);
			TestEqual(TEXT("same seed, same main"), Again.Main, Mix.Main);
			TestEqual(TEXT("same seed, same invaders"), Again.Invaders, Mix.Invaders);
			TestFalse(TEXT("a map always has a main faction"), Mix.Main.IsNone());
			TestTrue(TEXT("the main faction fights the crew"), DCFactionRules::FightsCrew(T.Factions, Mix.Main));
			TestFalse(TEXT("the main faction is not also an invader"), Mix.Invaders.Contains(Mix.Main));
			float Sum = Mix.MainShare;
			for (float S : Mix.InvaderShares)
			{
				Sum += S;
			}
			TestTrue(TEXT("shares sum to 1"), FMath::IsNearlyEqual(Sum, 1.f, 0.001f));
			TestTrue(TEXT("the main faction has the big share"), Mix.Invaders.Num() == 0 || Mix.MainShare > 0.5f);
			Mains.Add(Mix.Main);
		}
	}
	TestTrue(TEXT("seeds vary the main faction"), Mains.Num() >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCFactionDataTest, "Epic_AI_Mega_Jam2.Faction.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCFactionDataTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	const FName Army(TEXT("Army"));
	TestFalse(TEXT("the friendly army never fights the crew"), DCFactionRules::FightsCrew(T.Factions, Army));
	TestTrue(TEXT("the army fights the machines"), DCFactionRules::IsHostile(T.Factions, Army, TEXT("Machines")));
	TestTrue(TEXT("hostility is symmetric"), DCFactionRules::IsHostile(T.Factions, TEXT("Machines"), Army));
	TestFalse(TEXT("a faction never fights itself"), DCFactionRules::IsHostile(T.Factions, TEXT("Dead"), TEXT("Dead")));
	for (const FDCFactionDef& F : T.Factions)
	{
		TestTrue(*FString::Printf(TEXT("%s fields units"), *F.Id.ToString()), F.Units.Num() > 0);
		for (const FDCFactionUnit& U : F.Units)
		{
			const FDCEnemyDef* Def = T.FindEnemy(U.Enemy);
			TestNotNull(*FString::Printf(TEXT("%s unit %s is an +Enemies row"), *F.Id.ToString(), *U.Enemy.ToString()), Def);
			TestTrue(*FString::Printf(TEXT("%s unit %s belongs to it"), *F.Id.ToString(), *U.Enemy.ToString()), Def && Def->Faction == F.Id);
		}
		if (F.StrikeInterval > 0.f)
		{
			const FDCStratagemDef* S = T.FindStratagem(F.StrikeStratagem);
			TestNotNull(*FString::Printf(TEXT("%s strike resolves"), *F.Id.ToString()), S);
			TestTrue(*FString::Printf(TEXT("%s strike is never a player unlock"), *F.Id.ToString()), S && !S->bStartsUnlocked);
		}
		// Picks always land on one of its units.
		for (float Roll : {0.f, 0.37f, 0.999f, 1.f})
		{
			const FName Unit = DCFactionRules::PickUnit(F, Roll);
			TestTrue(TEXT("pick is a unit of the faction"), F.Units.ContainsByPredicate([Unit](const FDCFactionUnit& U) { return U.Enemy == Unit; }));
		}
	}
	// Drop picks follow the shares: roll 0 is the main faction, the top of the range an invader.
	FDCFactionMix Mix;
	Mix.Main = TEXT("Dead");
	Mix.MainShare = 0.7f;
	Mix.Invaders = {TEXT("Machines"), TEXT("Hive")};
	Mix.InvaderShares = {0.2f, 0.1f};
	TestEqual(TEXT("low roll: main"), DCFactionRules::PickDropFaction(Mix, 0.1f), FName(TEXT("Dead")));
	TestEqual(TEXT("mid roll: first invader"), DCFactionRules::PickDropFaction(Mix, 0.8f), FName(TEXT("Machines")));
	TestEqual(TEXT("top roll: last invader"), DCFactionRules::PickDropFaction(Mix, 0.95f), FName(TEXT("Hive")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCFactionTerritoryTest, "Epic_AI_Mega_Jam2.Faction.Territory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCFactionTerritoryTest::RunTest(const FString&)
{
	const FName Dead(TEXT("Dead")), Machines(TEXT("Machines"));
	TestEqual(TEXT("bottom-left zone"), DCFactionRules::ZoneOf(FVector(-900, -900, 0), 1000.f, 3), 0);
	TestEqual(TEXT("top-right zone"), DCFactionRules::ZoneOf(FVector(900, 900, 0), 1000.f, 3), 8);
	TestEqual(TEXT("outside clamps to the edge"), DCFactionRules::ZoneOf(FVector(5000, -5000, 0), 1000.f, 3), 2);
	TArray<FName> Owners;
	Owners.Init(Dead, 9);
	TArray<TPair<FVector, FName>> Units = {{FVector(900, 900, 0), Machines}};
	TestEqual(TEXT("one unit takes nothing (TakeMinUnits 2)"), DCFactionRules::StepTerritory(Owners, Units, 1000.f, 3, 2), 0);
	Units.Add({FVector(850, 880, 0), Machines});
	TestEqual(TEXT("two machines take the empty corner"), DCFactionRules::StepTerritory(Owners, Units, 1000.f, 3, 2), 1);
	TestEqual(TEXT("corner owner"), Owners[8], Machines);
	Units.Add({FVector(800, 800, 0), Dead});
	Units.Add({FVector(820, 800, 0), Dead});
	TestEqual(TEXT("a tie keeps the owner"), DCFactionRules::StepTerritory(Owners, Units, 1000.f, 3, 2), 0);
	Units.Add({FVector(840, 800, 0), Dead});
	DCFactionRules::StepTerritory(Owners, Units, 1000.f, 3, 2);
	TestEqual(TEXT("outnumbered: the dead take it back"), Owners[8], Dead);
	return true;
}

#endif
