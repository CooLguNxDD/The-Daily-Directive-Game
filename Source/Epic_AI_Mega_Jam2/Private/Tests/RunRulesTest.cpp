// Pure run rules and the shipped run data (Config/DefaultGame.ini): draft offers, scoops, critic order, page
// score, headlines, and that every upgrade, scoop and stratagem row resolves. Runs under -nullrhi.
#include "DCTuning.h"
#include "DCUpgrades.h"
#include "DCLanBeacon.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCPhotoRules.h"
#include "Rules/DCRunRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FDCUpgradeDef Up(const TCHAR* Id, const TCHAR* Pool)
	{
		FDCUpgradeDef D;
		D.Id = Id;
		D.Pool = Pool;
		D.Effect = TEXT("Gear");
		return D;
	}

	FDCPhotoRecord Shot(int32 Id, int32 Score)
	{
		FDCPhotoRecord R;
		R.PhotoId = Id;
		R.Grade.Score = Score;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCDraftOfferTest, "Epic_AI_Mega_Jam2.Run.DraftOffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCDraftOfferTest::RunTest(const FString&)
{
	const TArray<FDCUpgradeDef> Ups = {Up(TEXT("A1"), TEXT("A")), Up(TEXT("A2"), TEXT("A")), Up(TEXT("B1"), TEXT("B")), Up(TEXT("C1"), TEXT("C")), Up(TEXT("C2"), TEXT("C"))};
	const TArray<FName> Offer = DCRunRules::DraftOffer(42, Ups, {});
	TestEqual(TEXT("every open option of every pool"), Offer, TArray<FName>({TEXT("A1"), TEXT("A2"), TEXT("B1"), TEXT("C1"), TEXT("C2")}));
	TestEqual(TEXT("same seed, same offer"), DCRunRules::DraftOffer(42, Ups, {}), Offer);
	const TArray<FName> Taken = DCRunRules::DraftOffer(7, Ups, {TEXT("B1"), TEXT("A1")});
	TestEqual(TEXT("a fully taken pool offers nothing"), Taken.Num(), 3);
	TestTrue(TEXT("taken options never come back"), !Taken.Contains(TEXT("A1")) && Taken.Contains(TEXT("A2")));
	TestEqual(TEXT("a pool larger than PerPool is cut to PerPool"), DCRunRules::DraftOffer(1, Ups, {}, 1).Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCScoopRuleTest, "Epic_AI_Mega_Jam2.Run.Scoops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCScoopRuleTest::RunTest(const FString&)
{
	FDCScoopDef Two;
	Two.Metric = TEXT("Collapses");
	Two.Threshold = 2.f;
	FDCScoopDef Close;
	Close.Metric = TEXT("BlastMeters");
	Close.Threshold = 4.f;
	Close.bAtMost = true;
	FDCScoopDef Peak;
	Peak.Timing = TEXT("PEAK CINEMA");

	FDCPhotoInputs In;
	FDCPhotoGrade Grade;
	In.Metrics.Add(TEXT("Collapses"), 1.f);
	In.Metrics.Add(TEXT("BlastMeters"), 3.5f);
	TestFalse(TEXT("one collapse is not two"), DCRunRules::ScoopMet(Two, In, Grade));
	In.Metrics.Add(TEXT("Collapses"), 2.f);
	TestTrue(TEXT("two collapses in frame"), DCRunRules::ScoopMet(Two, In, Grade));
	TestTrue(TEXT("blast within 4 m"), DCRunRules::ScoopMet(Close, In, Grade));
	TestFalse(TEXT("timing scoop needs the label"), DCRunRules::ScoopMet(Peak, In, Grade));
	Grade.TimingLabel = TEXT("PEAK CINEMA");
	TestTrue(TEXT("peak cinema scoop"), DCRunRules::ScoopMet(Peak, In, Grade));

	FRandomStream Rng(3);
	for (int32 i = 0; i < 50; ++i)
	{
		const int32 Last = i % 5;
		const int32 Pick = DCRunRules::PickScoop(Rng, 5, Last);
		TestTrue(TEXT("next scoop differs from the last"), Pick != Last && Pick >= 0 && Pick < 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCPageRulesTest, "Epic_AI_Mega_Jam2.Run.FrontPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCPageRulesTest::RunTest(const FString&)
{
	const TArray<FDCPhotoRecord> Photos = {Shot(1, 300), Shot(2, 900), Shot(3, 900), Shot(4, 50)};
	const TArray<int32> Order = DCRunRules::CriticOrder(Photos);
	TestEqual(TEXT("critic: best first, earlier shot wins a tie"), Order, TArray<int32>({1, 2, 0, 3}));

	// A second take shot after the reel started is ranked into the final-cut list, not appended (C5.1).
	TArray<FDCPhotoRecord> WithTake = Photos;
	WithTake.Add(Shot(9, 600));
	TArray<int32> Choices = {2, 3, 1, 4};
	DCRunRules::InsertByCritic(Choices, WithTake, 9);
	TestEqual(TEXT("second take lands at its critic rank"), Choices, TArray<int32>({2, 3, 9, 1, 4}));
	DCRunRules::InsertByCritic(Choices, WithTake, 9);
	TestEqual(TEXT("no duplicate"), Choices.Num(), 5);
	WithTake.Add(Shot(10, 2000));
	DCRunRules::InsertByCritic(Choices, WithTake, 10);
	TestEqual(TEXT("best second take goes first, so the auto-pick sends it"), Choices[0], 10);

	TArray<FDCPagePick> Picks;
	Picks.AddDefaulted(2);
	Picks[0].PageScore = DCRunRules::PageScore(900, 0, 600);
	Picks[1].PageScore = DCRunRules::PageScore(400, 1, 600);
	TestEqual(TEXT("a vote can lift a weaker shot to the headline"), DCRunRules::HeadlineIndex(Picks), 1);
	TestEqual(TEXT("no picks, no headline"), DCRunRules::HeadlineIndex({}), -1);

	FDCPhotoGrade Grade;
	Grade.Categories = {TEXT("DEMOLITION")};
	Grade.TimingLabel = TEXT("PEAK CINEMA");
	const FString Line = DCRunRules::MakeHeadline(1, {TEXT("{PLAYER} TOPPLES {SUBJECT} IN {SECTOR} AT {TIMING}")}, {{TEXT("DEMOLITION"), TEXT("TOWER")}}, Grade, TEXT("Old Town"), TEXT("Ana"));
	TestEqual(TEXT("headline fills every slot"), Line, FString(TEXT("ANA TOPPLES TOWER IN OLD TOWN AT PEAK CINEMA")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCLanPacketTest, "Epic_AI_Mega_Jam2.Net.LanPacket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCLanPacketTest::RunTest(const FString&)
{
	int32 Port = 0, Players = 0;
	FString Name;
	TestTrue(TEXT("round trip"), UDCLanBeacon::ParsePacket(UDCLanBeacon::MakePacket(7777, TEXT("Press|Corps"), 2), Port, Name, Players));
	TestEqual(TEXT("port"), Port, 7777);
	TestEqual(TEXT("players"), Players, 2);
	TestEqual(TEXT("separator in a name is neutralised"), Name, FString(TEXT("Press Corps")));
	TestFalse(TEXT("foreign packet ignored"), UDCLanBeacon::ParsePacket(TEXT("HELLO|1|2|x"), Port, Name, Players));
	TestFalse(TEXT("bad port ignored"), UDCLanBeacon::ParsePacket(TEXT("DCHOST1|99999|1|x"), Port, Name, Players));
	TestFalse(TEXT("non-numeric ignored"), UDCLanBeacon::ParsePacket(TEXT("DCHOST1|abc|1|x"), Port, Name, Players));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCRunDataTest, "Epic_AI_Mega_Jam2.Run.ShippedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCRunDataTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	TMap<FName, int32> PerPool;
	for (const FDCUpgradeDef& U : T.Upgrades)
	{
		PerPool.FindOrAdd(U.Pool) += 1;
		TestTrue(FString::Printf(TEXT("upgrade %s has a known effect"), *U.Id.ToString()), DCUpgrades::IsKnownEffect(U.Effect));
		if (U.Effect == TEXT("UnlockStratagem"))
		{
			TestNotNull(FString::Printf(TEXT("upgrade %s unlocks a defined stratagem"), *U.Id.ToString()), T.FindStratagem(U.Unlock));
		}
	}
	TestEqual(TEXT("R10: three draft pools"), PerPool.Num(), 3);
	for (const TPair<FName, int32>& Pool : PerPool)
	{
		TestEqual(FString::Printf(TEXT("R10: pool %s has three options"), *Pool.Key.ToString()), Pool.Value, 3);
	}
	TestTrue(TEXT("R8: about five scoop templates"), T.Scoops.Num() >= 5);
	TestEqual(TEXT("R9: three sectors"), T.Sectors.Num(), 3);
	for (const FDCSectorDef& Sector : T.Sectors)
	{
		TestTrue(FString::Printf(TEXT("R6: sector %s has an HQ photo quota"), *Sector.Name.ToString()), Sector.QuotaPoints > 0);
	}
	TestEqual(TEXT("R7: 15 shots per sector"), T.Camera.ShotsPerSector, 15);
	const FDCTimingWindow* Over = DCPhotoRules::FindWindow(T.Photo, 0.f);
	const FDCTimingWindow* Peak = DCPhotoRules::FindWindow(T.Photo, 0.5f);
	TestTrue(TEXT("R4: Overexposed 1.5x at 0.0 s"), Over && Over->Label == TEXT("OVEREXPOSED") && FMath::IsNearlyEqual(Over->Multiplier, 1.5f));
	TestTrue(TEXT("R4: Peak Cinema 3.0x in 0.2-0.8 s"), Peak && Peak->Label == TEXT("PEAK CINEMA") && FMath::IsNearlyEqual(Peak->Multiplier, 3.f));
	TestTrue(TEXT("R15: headline templates exist"), T.Run.Headlines.Num() > 0);
	return true;
}

#endif
