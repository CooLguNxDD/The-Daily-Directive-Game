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
		return DCVoxelCraft::PlanDistrict(DCCityRules::SectorSeed(RunSeed, Sector), D, V.RuleSet(D.Rules), V.RuleSet(D.Props), V.SectorLayout(Sector), V.Recipes, UDCTuning::Get().City.BlockSizeCm);
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
	R.NoSameNeighbour.Reset();
	const TArray<FName> M = DCPcg::PickGrid(R, 7, 4, 3);
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
	for (const FDCPropPlan& Prop : P.Props)
	{
		Ctx.Models.Add(Prop.Model, DCVoxelModels::Parsed(Prop.Model));
	}
	FDCVoxelGrid Props;
	DCVoxelCraft::CraftProps(Props, P, Ctx);
	TestEqual(TEXT("prop grid is 5 cm (R1)"), Props.VoxelCm, V.Tuning.PropVoxelCm);
	TestTrue(TEXT("props have voxels"), Props.Data.CountSolid() > 1000);
	return true;
}

#endif
