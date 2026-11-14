// PCG rules (R15) and the city builder (R16) from the shipped district rows. Pure, runs under -nullrhi.
#include "DCTuning.h"
#include "DCVoxelCity.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCCityPlan.h"
#include "Rules/DCPcg.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DistrictFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FDCCityPlan Plan(int32 RunSeed, int32 Sector)
	{
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		const FDCDistrictRule& D = *V.District(Sector);
		return DCVoxelCraft::PlanDistrict(DCCityRules::SectorSeed(RunSeed, Sector), D, V.RuleSet(D.Rules), V.RuleSet(D.Props), V.SectorLayout(Sector), V.Recipes, UDCTuning::Get().City.BlockSizeCm, V.PropLayers);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCPcgRulesTest, "Epic_AI_Mega_Jam2.District.PcgRules", DistrictFlags)
bool FDCPcgRulesTest::RunTest(const FString&)
{
	FDCPcgRule R;
	R.Slot = TEXT("Lot");
	R.Choices = {{TEXT("A"), 1.f}, {TEXT("B"), 3.f}};
	R.NoSameNeighbour = {TEXT("A")};
	const TArray<FName> G1 = DCPcg::PickGrid(R, 99, 6, 6);
	TestTrue(TEXT("same seed, same grid"), G1 == DCPcg::PickGrid(R, 99, 6, 6));
	TestFalse(TEXT("another seed, another grid"), G1 == DCPcg::PickGrid(R, 100, 6, 6));
	bool bAdjacent = false;
	for (int32 Y = 0; Y < 6; ++Y)
	{
		for (int32 X = 0; X < 6; ++X)
		{
			bAdjacent |= G1[Y * 6 + X] == TEXT("A") && ((X + 1 < 6 && G1[Y * 6 + X + 1] == TEXT("A")) || (Y + 1 < 6 && G1[(Y + 1) * 6 + X] == TEXT("A")));
		}
	}
	TestFalse(TEXT("adjacency rule: no two A side by side"), bAdjacent);
	R.Mirror = TEXT("X");
	const TArray<FName> M = DCPcg::PickGrid(R, 7, 4, 3);
	bool bSeam = false;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		bSeam |= M[Y * 4 + 1] == TEXT("A") && M[Y * 4 + 2] == TEXT("A");
	}
	TestFalse(TEXT("adjacency holds across the mirror seam"), bSeam);
	bool bMirrored = true;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			bMirrored &= M[Y * 4 + X] == M[Y * 4 + 3 - X];
		}
	}
	TestTrue(TEXT("symmetry rule mirrors X"), bMirrored);
	int32 Bs = 0;
	for (int32 i = 0; i < 4000; ++i)
	{
		Bs += DCPcg::Pick(R, 5, i) == TEXT("B") ? 1 : 0;
	}
	TestTrue(TEXT("weights: B about 3 of 4"), Bs > 2800 && Bs < 3200);
	FDCPcgRule Range;
	Range.Slot = TEXT("N");
	Range.Min = 2.f;
	Range.Max = 5.f;
	bool bIn = true;
	for (int32 i = 0; i < 200; ++i)
	{
		const int32 N = DCPcg::RangeInt(Range, 3, i);
		bIn &= N >= 2 && N <= 5;
	}
	TestTrue(TEXT("ranges stay inside"), bIn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCDistrictBuilderTest, "Epic_AI_Mega_Jam2.District.CityBuilder", DistrictFlags)
bool FDCDistrictBuilderTest::RunTest(const FString&)
{
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TestTrue(TEXT("a district per sector"), V.Districts.Num() >= 3);
	for (int32 Sector = 0; Sector < 3; ++Sector)
	{
		const FDCDistrictRule& D = *V.District(Sector);
		const FDCCityPlan P = Plan(4242, Sector);
		TestTrue(FString::Printf(TEXT("sector %d: same seed, same plan"), Sector), P.LotKinds == Plan(4242, Sector).LotKinds && P.Props.Num() == Plan(4242, Sector).Props.Num());
		TestTrue(FString::Printf(TEXT("sector %d: landmark placed"), Sector), P.Buildings.IsValidIndex(P.Landmark) && V.Recipes[P.Buildings[P.Landmark].Spec.Recipe].Id == D.LandmarkRecipe);
		TestTrue(FString::Printf(TEXT("sector %d: at least MinTowers"), Sector), P.Buildings.Num() >= D.MinTowers);
		TestTrue(FString::Printf(TEXT("sector %d: props placed"), Sector), P.Props.Num() > 10);
		for (const FDCPropPlan& Prop : P.Props)
		{
			TestNotNull(*FString::Printf(TEXT("prop model %s exists"), *Prop.Model.ToString()), DCVoxelModels::Parsed(Prop.Model));
		}
		// Sight line: every lot it crosses (other than the landmark) is the district's open/low kind.
		const float Pitch = D.LotSizeCm + D.StreetWidthCm;
		for (int32 c = 0; c < P.LotKinds.Num(); ++c)
		{
			const FVector C((c % P.GridX) * Pitch - (P.GridX - 1) * Pitch * 0.5f, (c / P.GridX) * Pitch - (P.GridY - 1) * Pitch * 0.5f, 0.f);
			const FVector Dir = P.SightTo - P.SightFrom;
			const float T = FVector::DotProduct(C - P.SightFrom, Dir) / FMath::Max(Dir.SizeSquared2D(), 1.f);
			if (T > 0.f && T < 1.f && FVector::Dist2D(C, P.SightFrom + Dir * T) < D.LotSizeCm * 0.5f && P.LotKinds[c] != TEXT("Landmark"))
			{
				TestEqual(FString::Printf(TEXT("sector %d lot %d on the sight line"), Sector, c), P.LotKinds[c], D.SightLineKind);
			}
		}
		AddInfo(FString::Printf(TEXT("DCDISTRICT %s: %d buildings, %d plazas, %d rubble zones, %d props"), *D.Id.ToString(), P.Buildings.Num(), P.Plazas.Num(), P.RubbleZones.Num(), P.Props.Num()));
	}
	// Props stamp into the 5 cm prop grid.
	const FDCCityPlan P = Plan(4242, 0);
	FDCVoxelCraftContext Ctx = V.CraftContext(UDCTuning::Get().City.BlockSizeCm);
	for (const FName& Model : DCVoxelCraft::PlanModels(P))
	{
		Ctx.Models.Add(Model, DCVoxelModels::Parsed(Model));
	}
	FDCVoxelGrid Props;
	DCVoxelCraft::CraftProps(Props, P, Ctx);
	TestEqual(TEXT("prop grid is 5 cm (R1)"), Props.VoxelCm, V.Tuning.PropVoxelCm);
	TestTrue(TEXT("props have voxels"), Props.Data.CountSolid() > 1000);
	return true;
}

namespace
{
	/** Items per prop layer in a plan: ground props, building attachments and cables (R2). */
	TMap<FName, int32> LayerCounts(const FDCCityPlan& P, const TArray<FDCPropLayer>& Layers)
	{
		TMap<FName, int32> Out;
		for (const FDCPropPlan& Prop : P.Props)
		{
			++Out.FindOrAdd(Prop.Layer);
		}
		for (const FDCLotPlan& B : P.Buildings)
		{
			for (const FDCAttachPlan& A : B.Attach)
			{
				++Out.FindOrAdd(A.Layer);
			}
		}
		const FDCPropLayer* Cable = Layers.FindByPredicate([](const FDCPropLayer& L) { return L.Placer == TEXT("Cable"); });
		if (Cable)
		{
			Out.FindOrAdd(Cable->Id) += P.Cables.Num();
		}
		return Out;
	}

	FString PlanFingerprint(const FDCCityPlan& P)
	{
		FString S;
		for (const FDCPropPlan& Prop : P.Props)
		{
			S += FString::Printf(TEXT("%s@%s/%d;"), *Prop.Model.ToString(), *Prop.Location.ToString(), Prop.Turns);
		}
		for (const FDCLotPlan& B : P.Buildings)
		{
			for (const FDCAttachPlan& A : B.Attach)
			{
				S += FString::Printf(TEXT("%s:%s/%d/%.4f/%.4f;"), *A.Model.ToString(), *A.Mount.ToString(), A.Face, A.U, A.V);
			}
		}
		for (const FDCCablePlan& K : P.Cables)
		{
			S += FString::Printf(TEXT("cable %s-%s;"), *K.A.ToString(), *K.B.ToString());
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCCityPropsTest, "Epic_AI_Mega_Jam2.District.CityProps", DistrictFlags)
bool FDCCityPropsTest::RunTest(const FString&)
{
	// R2: every prop layer a district's rule set turns on places props; every model exists; the same seed gives the
	// same plan and the same crafted voxels; roof and wall mounts land in the tower grids.
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TestTrue(TEXT("prop layers declared"), V.PropLayers.Num() >= 8);
	TSet<FName> Models;
	for (int32 Sector = 0; Sector < 3; ++Sector)
	{
		const FDCDistrictRule& D = *V.District(Sector);
		const FDCPcgRuleSet* Rules = V.RuleSet(D.Props);
		const FDCCityPlan P = Plan(4242, Sector);
		TestEqual(FString::Printf(TEXT("sector %d: same seed, same props"), Sector), PlanFingerprint(P), PlanFingerprint(Plan(4242, Sector)));
		TestNotEqual(FString::Printf(TEXT("sector %d: another seed, other props"), Sector), PlanFingerprint(P), PlanFingerprint(Plan(4243, Sector)));
		const TMap<FName, int32> Counts = LayerCounts(P, V.PropLayers);
		FString Line;
		for (const FDCPropLayer& L : V.PropLayers)
		{
			// A layer bound to lot kinds only has spots where the plan rolled such a lot.
			const bool bSpots = L.LotKinds.Num() == 0 || L.LotKinds.ContainsByPredicate([&P](const FName& K) { return P.LotKinds.Contains(K); });
			const bool bOn = bSpots && Rules && Rules->Find(L.Id) && Rules->Find(FName(*(L.Id.ToString() + TEXT("Count"))));
			const int32 N = Counts.FindRef(L.Id);
			Line += FString::Printf(TEXT(" %s=%d"), *L.Id.ToString(), N);
			if (bOn)
			{
				TestTrue(FString::Printf(TEXT("sector %d: layer %s places props"), Sector, *L.Id.ToString()), N > 0);
			}
		}
		for (const FName& M : DCVoxelCraft::PlanModels(P))
		{
			Models.Add(M);
			TestNotNull(*FString::Printf(TEXT("model %s exists"), *M.ToString()), DCVoxelModels::Parsed(M));
		}
		AddInfo(FString::Printf(TEXT("DCPROPS %s:%s cables=%d"), *D.Id.ToString(), *Line, P.Cables.Num()));
	}
	TestTrue(TEXT("at least 24 distinct prop models across the districts"), Models.Num() >= 24);

	// Crafted twice from the same plan: the same voxels (host and client build the same hash).
	const FDCCityPlan P = Plan(4242, 0);
	FDCVoxelCraftContext Ctx = V.CraftContext(UDCTuning::Get().City.BlockSizeCm);
	for (const FName& M : DCVoxelCraft::PlanModels(P))
	{
		Ctx.Models.Add(M, DCVoxelModels::Parsed(M));
	}
	const double T0 = FPlatformTime::Seconds();
	FDCVoxelGrid PropsA, PropsB;
	DCVoxelCraft::CraftProps(PropsA, P, Ctx);
	const double T1 = FPlatformTime::Seconds();
	DCVoxelCraft::CraftProps(PropsB, P, Ctx);
	TestEqual(TEXT("props craft is deterministic"), PropsA.Data.ContentHash(), PropsB.Data.ContentHash());

	// A tower with its attachments has more voxels than the same lot without, and its scan box holds them.
	int32 WithAttach = 0;
	double TowerMs = 0.0;
	for (int32 b = 0; b < P.Buildings.Num(); ++b)
	{
		if (P.Buildings[b].Attach.Num() == 0)
		{
			continue;
		}
		FDCLotPlan Bare = P.Buildings[b];
		Bare.Attach.Reset();
		FDCVoxelTower A, B, C;
		const double S0 = FPlatformTime::Seconds();
		DCVoxelCraft::CraftTower(A, P.Buildings[b], b, Ctx);
		TowerMs += (FPlatformTime::Seconds() - S0) * 1000.0;
		DCVoxelCraft::CraftTower(B, Bare, b, Ctx);
		DCVoxelCraft::CraftTower(C, P.Buildings[b], b, Ctx);
		TestEqual(FString::Printf(TEXT("tower %d craft is deterministic"), b), A.Grid.Data.ContentHash(), C.Grid.Data.ContentHash());
		TestTrue(FString::Printf(TEXT("tower %d: attachments add voxels"), b), A.Grid.Data.CountSolid() > B.Grid.Data.CountSolid());
		TestTrue(FString::Printf(TEXT("tower %d: scan box holds the roof clutter"), b), A.ScanMax.Z >= B.ScanMax.Z && A.IntactLayers.Num() == A.ScanMax.Z);
		TestTrue(FString::Printf(TEXT("tower %d: attachment light spots recorded"), b), A.AttachSpots.Num() > 0 && A.AttachSpots.Num() <= P.Buildings[b].Attach.Num());
		for (const FDCVoxelAttachSpot& Spot : A.AttachSpots)
		{
			TestTrue(FString::Printf(TEXT("tower %d: %s spot above the street"), b, *Spot.Model.ToString()), Spot.CenterCm.Z > 0 && Spot.Normal.Size() == 1);
		}
		++WithAttach;
	}
	TestTrue(TEXT("towers carry roof clutter or wall mounts"), WithAttach >= 4);
	AddInfo(FString::Printf(TEXT("DCPROPS craft: props grid %.1f ms (%d solid), %d towers with attachments %.1f ms"), (T1 - T0) * 1000.0, static_cast<int32>(PropsA.Data.CountSolid()), WithAttach, TowerMs));
	return true;
}

#endif
