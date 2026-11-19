// R4 objectives: the pure stage table per kind, the seeded pick and placement helpers, and the shipped rows.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCObjectiveRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCObjectiveStagesTest, "Epic_AI_Mega_Jam2.Objective.Stages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCObjectiveStagesTest::RunTest(const FString&)
{
	using S = EDCObjectiveStage;
	const FName Retrieve(TEXT("Retrieve")), Destroy(TEXT("Destroy"));
	FDCObjectiveInputs In;
	TestEqual(TEXT("retrieve waits for the package"), DCObjectiveRules::Next(Retrieve, S::Active, In), S::Active);
	In.bCarried = true;
	TestEqual(TEXT("carried: the beacon opens"), DCObjectiveRules::Next(Retrieve, S::Active, In), S::Extract);
	TestEqual(TEXT("carried but not there yet"), DCObjectiveRules::Next(Retrieve, S::Extract, In), S::Extract);
	In.bAnyoneInZone = true;
	TestEqual(TEXT("someone else in the zone is not enough"), DCObjectiveRules::Next(Retrieve, S::Extract, In), S::Extract);
	In.bCarrierInZone = true;
	TestEqual(TEXT("the carrier in the zone: extracted"), DCObjectiveRules::Next(Retrieve, S::Extract, In), S::Done);
	FDCObjectiveInputs Dropped;
	TestEqual(TEXT("a dropped package keeps the beacon lit"), DCObjectiveRules::Next(Retrieve, S::Extract, Dropped), S::Extract);
	FDCObjectiveInputs D;
	D.Targets = 3;
	D.Destroyed = 2;
	TestEqual(TEXT("two of three down"), DCObjectiveRules::Next(Destroy, S::Active, D), S::Active);
	D.Destroyed = 3;
	TestEqual(TEXT("three of three down: extract"), DCObjectiveRules::Next(Destroy, S::Active, D), S::Extract);
	D.bAnyoneInZone = true;
	TestEqual(TEXT("anyone at the beacon extracts"), DCObjectiveRules::Next(Destroy, S::Extract, D), S::Done);
	FDCObjectiveInputs Late;
	Late.bTimeUp = true;
	TestEqual(TEXT("time up fails a live objective"), DCObjectiveRules::Next(Destroy, S::Active, Late), S::Failed);
	TestEqual(TEXT("time up fails extraction too"), DCObjectiveRules::Next(Retrieve, S::Extract, Late), S::Failed);
	TestEqual(TEXT("done is final"), DCObjectiveRules::Next(Retrieve, S::Done, Late), S::Done);
	TestEqual(TEXT("an unknown kind never moves"), DCObjectiveRules::Next(TEXT("Nope"), S::Active, Late), S::Active);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCObjectivePlaceTest, "Epic_AI_Mega_Jam2.Objective.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCObjectivePlaceTest::RunTest(const FString&)
{
	TArray<FVector> Towers;
	for (int32 y = 0; y < 4; ++y)
	{
		for (int32 x = 0; x < 4; ++x)
		{
			Towers.Add(FVector(x * 4200.f, y * 4200.f, 0.f));
		}
	}
	const TArray<int32> A = DCObjectiveRules::PickTargets(Towers, FVector::ZeroVector, 3, 5000.f, 77);
	TestEqual(TEXT("three targets"), A.Num(), 3);
	TestEqual(TEXT("same seed, same targets"), DCObjectiveRules::PickTargets(Towers, FVector::ZeroVector, 3, 5000.f, 77), A);
	for (int32 i = 0; i < A.Num(); ++i)
	{
		for (int32 j = i + 1; j < A.Num(); ++j)
		{
			TestTrue(TEXT("targets apart"), FVector::Dist2D(Towers[A[i]], Towers[A[j]]) >= 5000.f);
		}
	}
	TestEqual(TEXT("a tiny map still gives three"), DCObjectiveRules::PickTargets({FVector(0), FVector(100, 0, 0), FVector(200, 0, 0)}, FVector::ZeroVector, 3, 5000.f, 1).Num(), 3);
	TestEqual(TEXT("farthest point"), DCObjectiveRules::Farthest(Towers, FVector::ZeroVector), 15);
	TSet<int32> Deeps;
	for (int32 Seed = 0; Seed < 30; ++Seed)
	{
		const int32 D = DCObjectiveRules::Deep(Towers, FVector::ZeroVector, 0.25f, Seed * 31);
		TestTrue(TEXT("deep point is in the far quarter"), FVector::Dist2D(Towers[D], FVector::ZeroVector) >= FVector::Dist2D(Towers[10], FVector::ZeroVector) - 1.f);
		Deeps.Add(D);
	}
	TestTrue(TEXT("seeds vary the deep point"), Deeps.Num() >= 2);
	const int32 E = DCObjectiveRules::AtLeast(Towers, FVector::ZeroVector, 8000.f);
	TestTrue(TEXT("extraction at least the distance away"), FVector::Dist2D(Towers[E], FVector::ZeroVector) >= 8000.f);
	// Shipped rows: both kinds, picks vary with the seed.
	const UDCTuning& T = UDCTuning::Get();
	TSet<FName> Kinds;
	for (int32 Seed = 1; Seed < 40; ++Seed)
	{
		const FDCObjectiveDef* Def = DCObjectiveRules::Pick(T.Objectives, Seed * 104729, Seed % 3);
		TestNotNull(TEXT("an objective every sector"), Def);
		Kinds.Add(Def ? Def->Kind : NAME_None);
	}
	TestTrue(TEXT("both kinds come up"), Kinds.Contains(TEXT("Retrieve")) && Kinds.Contains(TEXT("Destroy")));
	return true;
}

#endif
