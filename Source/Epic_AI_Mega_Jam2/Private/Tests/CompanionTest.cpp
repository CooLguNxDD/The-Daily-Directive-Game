// R22 card kinds: the pure companion rules (movers, target pick, deploy ground), the Spiral strike pattern, and the
// shipped card data (every new card resolves its companion, stratagem, rarity, effect and model). Runs under -nullrhi.
#include "DCTuning.h"
#include "DCUpgrades.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCCompanionRules.h"
#include "Rules/DCFactionRules.h"
#include "Rules/DCStrikePattern.h"
#include "Voxel/DCVoxelModel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCCompanionRulesTest, "Epic_AI_Mega_Jam2.Cards.CompanionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCCompanionRulesTest::RunTest(const FString&)
{
	FDCCompanionDef Orbit;
	Orbit.Mover = TEXT("Orbit");
	Orbit.OrbitRadiusCm = 200.f;
	Orbit.HeightCm = 150.f;
	Orbit.OrbitSeconds = 4.f;
	Orbit.BobCm = 0.f;
	const FVector Owner(1000.f, -500.f, 90.f);
	const FVector P0 = DCCompanionRules::Position(Orbit, Owner, 0.f, 0, 1);
	const FVector P1 = DCCompanionRules::Position(Orbit, Owner, 1.f, 0, 1);
	TestEqual(TEXT("orbit keeps its radius"), static_cast<float>(FVector::Dist2D(P0, Owner)), 200.f, 0.5f);
	TestEqual(TEXT("orbit keeps its height over the owner"), static_cast<float>(P0.Z), static_cast<float>(Owner.Z + 150.f), 0.5f);
	TestEqual(TEXT("a quarter lap after a quarter of OrbitSeconds (90 degrees = radius * sqrt 2 apart)"), static_cast<float>(FVector::Dist2D(P0, P1)), 200.f * UE_SQRT_2, 1.f);
	const FVector Slot1 = DCCompanionRules::Position(Orbit, Owner, 0.f, 1, 2);
	TestEqual(TEXT("two drones of one owner sit opposite"), static_cast<float>(FVector::Dist2D(P0, Slot1)), 400.f, 1.f);
	Orbit.BobCm = 20.f;
	Orbit.BobHz = 1.f;
	const FVector Bobbed = DCCompanionRules::Position(Orbit, Owner, 0.25f, 0, 1);
	TestTrue(TEXT("bob stays within BobCm"), FMath::Abs(Bobbed.Z - (Owner.Z + 150.f)) <= 20.01f);

	FDCCompanionDef Fixed;
	Fixed.Mover = TEXT("Fixed");
	Fixed.HeightCm = 0.f;
	const FVector Home(300.f, 300.f, 12.f);
	TestTrue(TEXT("a fixed companion stays on its spot at any time"), DCCompanionRules::Position(Fixed, Home, 37.f, 0, 1).Equals(Home, 0.01f));
	FDCCompanionDef Unknown;
	Unknown.Mover = TEXT("Teleport");
	TestFalse(TEXT("unknown mover is not registered"), DCCompanionRules::IsKnownMover(Unknown.Mover));
	TestTrue(TEXT("an unknown mover stays on the anchor"), DCCompanionRules::Position(Unknown, Home, 5.f, 0, 1).Equals(Home, 0.01f));

	const TArray<FVector> Foes = {FVector(900, 0, 0), FVector(400, 0, 0), FVector(0, 3000, 0)};
	TestEqual(TEXT("nearest foe in range"), DCCompanionRules::PickTarget(FVector::ZeroVector, 1000.f, Foes), 1);
	TestEqual(TEXT("nothing in range"), DCCompanionRules::PickTarget(FVector::ZeroVector, 300.f, Foes), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no candidates"), DCCompanionRules::PickTarget(FVector::ZeroVector, 1000.f, {}), static_cast<int32>(INDEX_NONE));

	FDCCompanionDef Turret;
	Turret.PlaceDistanceCm = 250.f;
	Turret.MaxStepCm = 150.f;
	const FVector Aim = DCCompanionRules::PlaceAim(Turret, FVector(0, 0, 100.f), FVector(0.f, 1.f, -0.8f));
	TestTrue(TEXT("deploy aims PlaceDistanceCm ahead on the ground plane (a look down does not shorten it)"), Aim.Equals(FVector(0, 250.f, 100.f), 0.5f));
	TestTrue(TEXT("a straight-down look still deploys ahead"), FVector::Dist2D(DCCompanionRules::PlaceAim(Turret, FVector::ZeroVector, FVector(0, 0, -1)), FVector::ZeroVector) > 249.f);
	TestTrue(TEXT("ground a kerb higher is fine"), DCCompanionRules::PlaceOk(Turret, 100.f, 120.f));
	TestFalse(TEXT("a rooftop above is refused"), DCCompanionRules::PlaceOk(Turret, 100.f, 900.f));
	TestFalse(TEXT("a drop below is refused"), DCCompanionRules::PlaceOk(Turret, 100.f, -200.f));

	// Spiral (Carpet Bomb): the first on the target, the last at the span, distances growing outward.
	FRandomStream Rng(3);
	const FVector2D Along(1.f, 0.f);
	float Last = -1.f;
	bool bOutward = true;
	for (int32 i = 0; i < 9; ++i)
	{
		const float D = static_cast<float>(DCStrikePattern::Offset(TEXT("Spiral"), {i, 9, 3200.f, Along}, Rng).Size());
		bOutward &= D > Last;
		Last = D;
	}
	TestTrue(TEXT("spiral is registered"), DCStrikePattern::Known(TEXT("Spiral")));
	TestTrue(TEXT("spiral blasts walk outward"), bOutward);
	TestEqual(TEXT("spiral ends at the span"), Last, 3200.f, 1.f);
	TestTrue(TEXT("spiral starts on the target"), DCStrikePattern::Offset(TEXT("Spiral"), {0, 9, 3200.f, Along}, Rng).IsNearlyZero(0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCCardDataTest, "Epic_AI_Mega_Jam2.Cards.ShippedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCCardDataTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	for (const FDCCompanionDef& C : T.Companions)
	{
		const FString Id = C.Id.ToString();
		TestTrue(*FString::Printf(TEXT("companion %s mover is registered"), *Id), DCCompanionRules::IsKnownMover(C.Mover));
		TestTrue(*FString::Printf(TEXT("companion %s shoots (range, rate, damage)"), *Id), C.RangeCm > 0.f && C.FireSeconds > 0.f && C.Damage > 0.f);
		TestTrue(*FString::Printf(TEXT("companion %s model %s is forged"), *Id, *C.Model.ToString()), C.Model.IsNone() || DCVoxelModels::Parsed(C.Model) != nullptr);
	}
	TestTrue(TEXT("the pool can hold a drone and two turrets for four players"), T.CompanionPoolMax >= 12 && T.CompanionPrewarm <= T.CompanionPoolMax);
	// R22: the five new kinds ship, each Rare or Epic, each resolving what it needs.
	struct FKind { const TCHAR* Card; const TCHAR* Effect; };
	const FKind Kinds[] = {{TEXT("CarpetBomb"), TEXT("UnlockStratagem")}, {TEXT("GunDrone"), TEXT("Companion")}, {TEXT("Sentry"), TEXT("Deploy")},
		{TEXT("Reinforcements"), TEXT("UnlockStratagem")}, {TEXT("DeadMansSwitch"), TEXT("LastStand")}};
	for (const FKind& K : Kinds)
	{
		const FDCUpgradeDef* U = T.FindUpgrade(K.Card);
		if (!TestNotNull(*FString::Printf(TEXT("R22 card %s ships"), K.Card), U))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s effect is %s"), K.Card, K.Effect), U->Effect == FName(K.Effect));
		TestTrue(*FString::Printf(TEXT("%s is Rare or Epic"), K.Card), U->Rarity == TEXT("Rare") || U->Rarity == TEXT("Epic"));
		TestTrue(*FString::Printf(TEXT("%s card art %s loads"), K.Card, *U->Icon.ToString()), U->Icon.IsValid() && U->Icon.TryLoad() != nullptr);
	}
	for (const FDCUpgradeDef& U : T.Upgrades)
	{
		const bool bNeedsCompanion = U.Effect == TEXT("Companion") || U.Effect == TEXT("Deploy");
		TestTrue(*FString::Printf(TEXT("%s names a +Companions row"), *U.Id.ToString()), !bNeedsCompanion || T.FindCompanion(U.Companion) != nullptr);
		TestTrue(*FString::Printf(TEXT("%s deals at least one drone or charge"), *U.Id.ToString()), !bNeedsCompanion || U.Count > 0);
		TestTrue(*FString::Printf(TEXT("%s last stand names a stratagem"), *U.Id.ToString()), U.Effect != TEXT("LastStand") || T.FindStratagem(U.Unlock) != nullptr);
	}
	const FDCStratagemDef* Reinforce = T.FindStratagem(TEXT("Reinforcements"));
	if (TestNotNull(TEXT("Reinforcements stratagem"), Reinforce))
	{
		TestTrue(TEXT("Reinforcements uses the Reinforce call effect"), Reinforce->Effect == FName(TEXT("Reinforce")));
		TestTrue(TEXT("Reinforcements drops a squad"), Reinforce->SupportCount > 0);
		TestTrue(TEXT("Reinforcements faction exists and is crew-friendly"), DCFactionRules::Find(T.Factions, Reinforce->SupportFaction) != nullptr
			&& !DCFactionRules::FightsCrew(T.Factions, Reinforce->SupportFaction));
	}
	const FDCStratagemDef* Switch = T.FindStratagem(TEXT("DeadMansSwitch"));
	if (TestNotNull(TEXT("Dead Man's Switch stratagem"), Switch))
	{
		TestTrue(TEXT("Dead Man's Switch spares the crew and is never a hand call"), Switch->PlayerDamage <= 0.f && !Switch->bStartsUnlocked);
	}
	return true;
}

#endif
