// Voxel engine core (R1-R3): crafting from the shipped recipes, deterministic carve/structure/settle, greedy mesher.
// Pure: runs under -nullrhi with the real Config/DefaultGame.ini data.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelMesher.h"
#include "Voxel/DCVoxelOps.h"
#include "Voxel/DCVoxelSettings.h"
#include "Voxel/DCVoxelWorld.h"
#include "Voxel/DCVoxelModel.h"
#include "HAL/FileManager.h"
#include "Algo/Reverse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags VoxelFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FDCCityPlan VoxelPlan(int32 Seed, int32 Sector)
	{
		const UDCTuning& T = UDCTuning::Get();
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		FDCCityPlan Plan = DCCityRules::PlanCity(Seed, T.City, T.Look.BuildingColors.Num());
		DCVoxelCraft::ApplyRecipes(Plan, V.SectorLayout(Sector), V.Recipes, FMath::FloorToInt(T.City.LotSizeCm / T.City.BlockSizeCm));
		return Plan;
	}

	void Craft(FDCVoxelSector& S, const FDCCityPlan& Plan)
	{
		const FDCVoxelCraftContext Ctx = UDCVoxelSettings::Get().CraftContext(UDCTuning::Get().City.BlockSizeCm);
		DCVoxelCraft::CraftCity(Plan, Ctx, S.Towers);
		DCVoxelCraft::CraftTerrain(S.Terrain, Plan, UDCTuning::Get().City.LotSizeCm, Ctx);
	}

	FDCVoxelOp StrikeAt(const FDCVoxelTower& T, int32 Seq, int32 BlastRadiusCm)
	{
		FDCVoxelOp Op;
		Op.Seq = Seq;
		Op.Kind = TEXT("Strike");
		Op.Stratagem = TEXT("OrbitalLaser");
		// On the street 2 m off the -X face of the tower, like a thrown beacon.
		Op.CenterCm = FIntVector(T.FootMinCm().X - 200, (T.FootMinCm().Y + T.FootMaxCm().Y) / 2, 0);
		Op.BlastRadiusCm = BlastRadiusCm;
		return Op;
	}

	int64 CountTowerLayer(const FDCVoxelTower& T, int32 Z)
	{
		return DCVoxelCraft::CountLayer(T.Grid.Data, T.ScanMin, T.ScanMax, Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelCraftTest, "Epic_AI_Mega_Jam2.Voxel.CraftDeterministic", VoxelFlags)
bool FDCVoxelCraftTest::RunTest(const FString&)
{
	for (int32 Sector = 0; Sector < 3; ++Sector)
	{
		const FDCCityPlan Plan = VoxelPlan(DCCityRules::SectorSeed(1234, Sector), Sector);
		FDCVoxelSector A, B;
		const double T0 = FPlatformTime::Seconds();
		Craft(A, Plan);
		const double CraftMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		Craft(B, VoxelPlan(DCCityRules::SectorSeed(1234, Sector), Sector));
		TestTrue(FString::Printf(TEXT("sector %d has towers and ground"), Sector), A.Towers.Num() > 0 && A.Terrain.Data.CountSolid() > 0);
		TestEqual(FString::Printf(TEXT("sector %d: same seed, same voxels"), Sector), A.Hash(), B.Hash());
		int64 Voxels = 0;
		int32 Chunks = 0;
		for (const FDCVoxelTower& T : A.Towers)
		{
			const FDCBuildingSpec& Spec = Plan.Buildings[T.Lot].Spec;
			TestEqual(TEXT("tower height matches the gameplay spec"), T.Max.Z * T.Grid.VoxelCm, FMath::RoundToInt32(Spec.Floors * Spec.BlockSize));
			TestTrue(TEXT("intact layers counted"), T.IntactLayers.Num() == T.ScanMax.Z && T.IntactLayers[1] > 0);
			TestTrue(TEXT("hollow: a mid-floor layer is far from full"), CountTowerLayer(T, T.Max.Z / 2 + 5) * 3 < static_cast<int64>(T.Max.X - T.Min.X) * (T.Max.Y - T.Min.Y));
			Voxels += T.Grid.Data.CountSolid();
			Chunks += T.Grid.Data.NumChunks();
		}
		AddInfo(FString::Printf(TEXT("DCVOXEL craft sector %d: %d towers, %lld tower voxels, %d chunks, terrain %lld voxels, %.0f ms"), Sector, A.Towers.Num(), Voxels, Chunks, A.Terrain.Data.CountSolid(), CraftMs));
	}
	FDCVoxelSector C, D;
	Craft(C, VoxelPlan(1, 0));
	Craft(D, VoxelPlan(2, 0));
	TestNotEqual(TEXT("different seed, different city"), C.Hash(), D.Hash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelStrikeTest, "Epic_AI_Mega_Jam2.Voxel.StrikeTopplesAndSettles", VoxelFlags)
bool FDCVoxelStrikeTest::RunTest(const FString&)
{
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	const FDCCityPlan Plan = VoxelPlan(DCCityRules::SectorSeed(77, 0), 0);
	FDCVoxelSector S;
	Craft(S, Plan);
	const int32 Lot = 0;
	const int32 TopLayer = S.Towers[Lot].Max.Z - 2;
	const FIntVector FootMin = S.Towers[Lot].FootMinCm();
	const FIntVector FootMax = S.Towers[Lot].FootMaxCm();

	const double T0 = FPlatformTime::Seconds();
	const FDCVoxelOpResult R = DCVoxelOps::Apply(S, StrikeAt(S.Towers[Lot], 1, 1300), Ctx);
	AddInfo(FString::Printf(TEXT("DCVOXEL strike op: carved %d, pieces %d, rubble %d, %.0f ms (carve %.0f, structure %.0f, settle %.0f)"), R.Carved, R.Pieces.Num(), R.Rubble.Num(),
		(FPlatformTime::Seconds() - T0) * 1000.0, R.CarveMs, R.StructureMs, R.SettleMs));
	TestTrue(TEXT("the strike carved voxels"), R.Carved > 0);
	TestTrue(TEXT("the tower in the blast fell (structure pass)"), R.FallenLots.Contains(Lot));
	TestTrue(TEXT("a toppled piece broke off"), R.Pieces.ContainsByPredicate([Lot](const FDCVoxelPiece& P) { return P.bTopple && P.Lot == Lot && P.Cells.Num() > 1000; }));
	TestTrue(TEXT("the piece settled as rubble in the terrain"), R.Rubble.Num() > 100);
	TestEqual(TEXT("nothing stands at the top"), CountTowerLayer(S.Towers[Lot], TopLayer), 0ll);
	TestTrue(TEXT("a stump stands below the break"), CountTowerLayer(S.Towers[Lot], 0) > 0);
	// Rubble falls away from the blast: the strike was on -X, so its mean x lies past the tower centre.
	int64 SumX = 0;
	for (const FIntVector& P : R.Rubble)
	{
		SumX += S.Terrain.CenterCm(P).X;
	}
	const int64 MeanX = SumX / FMath::Max(1, R.Rubble.Num());
	TestTrue(TEXT("rubble lands away from the blast"), MeanX > (FootMin.X + FootMax.X) / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelDeterminismTest, "Epic_AI_Mega_Jam2.Voxel.OpsDeterministic", VoxelFlags)
bool FDCVoxelDeterminismTest::RunTest(const FString&)
{
	// Two machines: same crafted city, same op list, applied in order. Same voxels in every grid, rubble included.
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	const FDCCityPlan Plan = VoxelPlan(DCCityRules::SectorSeed(9001, 1), 1);
	FDCVoxelSector A, B;
	Craft(A, Plan);
	Craft(B, Plan);
	TArray<FDCVoxelOp> Ops;
	for (int32 i = 0; i < FMath::Min(3, A.Towers.Num()); ++i)
	{
		Ops.Add(StrikeAt(A.Towers[i], i + 1, 1100));
	}
	FDCVoxelOp Fell;
	Fell.Seq = Ops.Num() + 1;
	Fell.Kind = TEXT("Fell");
	Fell.Lot = A.Towers.Num() - 1;
	Fell.CenterCm = FIntVector(A.Towers.Last().FootMaxCm().X + 500, A.Towers.Last().FootMinCm().Y, 0);
	Ops.Add(Fell);
	for (const FDCVoxelOp& Op : Ops)
	{
		DCVoxelOps::Apply(A, Op, Ctx);
		DCVoxelOps::Apply(B, Op, Ctx);
		TestEqual(FString::Printf(TEXT("op %d: both machines hold the same voxels"), Op.Seq), A.Hash(), B.Hash());
	}
	TestTrue(TEXT("the felled lot fell"), A.Towers.Last().bFallen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelNoFloatTest, "Epic_AI_Mega_Jam2.Voxel.RubbleNeverFloats", VoxelFlags)
bool FDCVoxelNoFloatTest::RunTest(const FString&)
{
	// Strike every tower in turn (rubble lands on neighbours' roofs, then those neighbours fall): afterwards every
	// rubble cell rests on something - the cell below, or a tower voxel right under it (its real column top, never the
	// uncarved roof prism: a carved roof leaves no rubble sheet hanging).
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	FDCVoxelSector S;
	Craft(S, VoxelPlan(DCCityRules::SectorSeed(31337, 2), 2));
	TArray<FIntVector> Craters;
	for (int32 i = 0; i < S.Towers.Num(); ++i)
	{
		const FDCVoxelOp Op = StrikeAt(S.Towers[i], i + 1, 1300);
		Craters.Add(Op.CenterCm);
		DCVoxelOps::Apply(S, Op, Ctx);
	}
	const FDCVoxelGrid& G = S.Terrain;
	const int32 Ground = UDCVoxelSettings::Get().Tuning.GroundLayers;
	// No crater opens onto the void: the bedrock layers under every blast centre stay solid.
	const int32 Bedrock = UDCVoxelSettings::Get().Tuning.BedrockLayers;
	TestTrue(TEXT("bedrock is below the street"), Bedrock >= 1 && Bedrock < Ground);
	for (const FIntVector& C : Craters)
	{
		const FIntVector L = G.ToLocal(C);
		TestTrue(FString::Printf(TEXT("bedrock under the crater at %s"), *C.ToString()), G.Data.IsSolid(FIntVector(L.X, L.Y, 0)));
	}
	int32 Floating = 0;
	int32 Checked = 0;
	G.Data.ForEachChunk([&](const FIntVector& Key, const FDCVoxelChunk& C)
	{
		for (int32 i = 0; i < FDCVoxelChunk::Volume; ++i)
		{
			if (C.V[i] == 0)
			{
				continue;
			}
			const FIntVector P = Key * FDCVoxelChunk::Size + FIntVector(i & 31, (i >> 5) & 31, i >> 10);
			if (P.Z <= Ground)
			{
				continue; // street layers and kerbs
			}
			++Checked;
			if (G.Data.IsSolid(P - FIntVector(0, 0, 1)))
			{
				continue;
			}
			const bool bOnTower = S.Towers.ContainsByPredicate([&G, &P](const FDCVoxelTower& T)
			{
				return DCVoxelOps::TowerTopCells(T, G, P.X, P.Y) == P.Z;
			});
			Floating += bOnTower ? 0 : 1;
		}
	});
	AddInfo(FString::Printf(TEXT("DCVOXEL rubble cells %d, floating %d"), Checked, Floating));
	TestTrue(TEXT("rubble exists"), Checked > 100);
	TestEqual(TEXT("no rubble floats"), Floating, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelSightTest, "Epic_AI_Mega_Jam2.Voxel.PhotoSight", VoxelFlags)
bool FDCVoxelSightTest::RunTest(const FString&)
{
	// Photo sight marches the voxels: a blast's own struck tower never hides it from the street, another standing tower
	// between the lens and the subject does (CP4 review: physics traces lost PEAK to the struck shell).
	FDCVoxelSector S;
	Craft(S, VoxelPlan(DCCityRules::SectorSeed(31337, 1), 1));
	TestTrue(TEXT("two towers"), S.Towers.Num() >= 2);
	auto Center = [](const FDCVoxelTower& T)
	{
		const FIntVector A = T.FootMinCm(), B = T.FootMaxCm();
		return FVector((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f, T.Grid.OriginCm.Z + T.Max.Z * T.Grid.VoxelCm * 0.4f);
	};
	auto HalfSize = [](const FDCVoxelTower& T) { return FVector(T.FootMaxCm() - T.FootMinCm()).Size2D() * 0.5f; };
	int32 Other = 1;
	for (int32 i = 2; i < S.Towers.Num(); ++i)
	{
		Other = FVector::Dist2D(Center(S.Towers[i]), Center(S.Towers[0])) < FVector::Dist2D(Center(S.Towers[Other]), Center(S.Towers[0])) ? i : Other;
	}
	const FVector A = Center(S.Towers[0]);
	const FVector B = Center(S.Towers[Other]);
	const FVector Away = FVector(A - B).GetSafeNormal2D();
	const FVector Street = A + Away * (HalfSize(S.Towers[0]) + 300.f);
	const FVector Behind = B - Away * (HalfSize(S.Towers[Other]) + 300.f);
	auto Blocked = [&S](const FVector& From, const FVector& To, const TArray<int32>& Ignore)
	{
		return DCVoxelOps::SightBlocked(S, From, To, Ignore, 25.f, 60.f, 250.f);
	};
	TestTrue(TEXT("the struck tower's own wall does not hide its blast"), !Blocked(Street, A, {0}));
	TestTrue(TEXT("the same wall hides it when the tower is not the subject"), Blocked(Street, A, {}));
	TestTrue(TEXT("another standing tower between lens and subject hides it"), Blocked(Behind, A, {0}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelSmallCarveTest, "Epic_AI_Mega_Jam2.Voxel.SmallHoleStands", VoxelFlags)
bool FDCVoxelSmallCarveTest::RunTest(const FString&)
{
	// A hole punched high in a wall, with no blast bite, leaves the tower standing (only islands drop).
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	FDCVoxelSector S;
	Craft(S, VoxelPlan(DCCityRules::SectorSeed(5, 0), 0));
	FDCVoxelTower& T = S.Towers[0];
	const FIntVector Mid(T.FootMinCm().X, (T.FootMinCm().Y + T.FootMaxCm().Y) / 2, T.Max.Z * T.Grid.VoxelCm / 2);
	TestTrue(TEXT("hole carved"), DCVoxelOps::CarveSphere(T.Grid, Mid, 150) > 0);
	FDCVoxelOpResult R;
	DCVoxelOps::Structure(T, FIntVector(1, 0, 0), Ctx, R);
	TestTrue(TEXT("tower still stands"), R.FallenLots.Num() == 0 && !T.bFallen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelDataTest, "Epic_AI_Mega_Jam2.Voxel.ShippedData", VoxelFlags)
bool FDCVoxelDataTest::RunTest(const FString&)
{
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TestTrue(TEXT("voxel city is on with recipes and a palette"), V.IsVoxelCity());
	TestEqual(TEXT("building shells default to 10 cm (R1)"), V.Tuning.TowerVoxelCm, 10);
	TestTrue(TEXT("ground and rubble 25-100 cm (R1)"), V.Tuning.TerrainVoxelCm >= 25 && V.Tuning.TerrainVoxelCm <= 100);
	TestTrue(TEXT("props 5 cm or more (R1)"), V.Tuning.PropVoxelCm >= 5);
	TestTrue(TEXT("palette fits a byte"), V.Palette.Num() < 255);
	for (const FDCVoxelRecipe& R : V.Recipes)
	{
		for (FName Id : {R.Wall, R.Window, R.Trim, R.Slab, R.Poster})
		{
			TestTrue(FString::Printf(TEXT("recipe %s material %s is in the palette"), *R.Id.ToString(), *Id.ToString()),
				V.Palette.ContainsByPredicate([Id](const FDCVoxelMaterial& M) { return M.Id == Id; }));
		}
		TestTrue(FString::Printf(TEXT("recipe %s fits a lot"), *R.Id.ToString()), R.MaxBays * UDCTuning::Get().City.BlockSizeCm <= UDCTuning::Get().City.LotSizeCm);
	}
	TestTrue(TEXT("three sector layouts (R2: all three sectors from data)"), V.Sectors.Num() >= 3);
	for (const FDCVoxelSectorLayout& L : V.Sectors)
	{
		for (const FDCVoxelRecipeWeight& W : L.Recipes)
		{
			TestTrue(FString::Printf(TEXT("layout recipe %s exists"), *W.Recipe.ToString()), V.Recipes.ContainsByPredicate([&W](const FDCVoxelRecipe& R) { return R.Id == W.Recipe; }));
		}
	}
	for (const FDCStratagemDef& S : UDCTuning::Get().Stratagems)
	{
		TestTrue(FString::Printf(TEXT("stratagem %s has a carve row"), *S.Id.ToString()), V.Carves.ContainsByPredicate([&S](const FDCVoxelCarveDef& C) { return C.Stratagem == S.Id; }));
	}
	// Palette discipline (R6): no voxel material is propaganda red; red stays for danger.
	for (const FDCVoxelMaterial& M : V.Palette)
	{
		const bool bRed = M.Color.R > 0.3f && M.Color.G < 0.1f && M.Color.B < 0.1f;
		TestFalse(FString::Printf(TEXT("palette %s is not red (only Danger may be)"), *M.Id.ToString()), bRed && M.Id != TEXT("Danger"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelMesherTest, "Epic_AI_Mega_Jam2.Voxel.GreedyMesher", VoxelFlags)
bool FDCVoxelMesherTest::RunTest(const FString&)
{
	const TArray<FColor> Colors = {FColor::Transparent, FColor(40, 40, 40, 0), FColor(200, 200, 200, 128)};
	FDCVoxelWorld W;
	W.Set(FIntVector(3, 3, 3), 1);
	TArray<uint8> Padded;
	W.CopyPadded(FIntVector(0, 0, 0), Padded);
	FDCVoxelMeshData M;
	DCVoxelMesher::MeshPadded(Padded, Colors, 25.f, 0.f, M);
	TestEqual(TEXT("one voxel: six quads"), M.Indices.Num(), 6 * 6);

	// A 4x4x1 slab: greedy merges every face into one quad per side.
	W.Reset();
	W.FillBox(FIntVector(0, 0, 0), FIntVector(4, 4, 1), 1);
	W.CopyPadded(FIntVector(0, 0, 0), Padded);
	M.Reset();
	DCVoxelMesher::MeshPadded(Padded, Colors, 25.f, 0.f, M);
	TestEqual(TEXT("slab: six merged quads"), M.Indices.Num(), 6 * 6);

	// Every triangle faces out: Unreal front face means (B-A)x(C-A) opposes the normal.
	bool bAllOut = true;
	for (int32 i = 0; i < M.Indices.Num(); i += 3)
	{
		const FVector& A = M.Positions[M.Indices[i]];
		const FVector& B = M.Positions[M.Indices[i + 1]];
		const FVector& C = M.Positions[M.Indices[i + 2]];
		bAllOut &= FVector::DotProduct(FVector::CrossProduct(B - A, C - A), M.Normals[M.Indices[i]]) < 0.f;
	}
	TestTrue(TEXT("winding faces out"), bAllOut);

	// Two materials side by side do not merge.
	W.Reset();
	W.Set(FIntVector(0, 0, 0), 1);
	W.Set(FIntVector(1, 0, 0), 2);
	W.CopyPadded(FIntVector(0, 0, 0), Padded);
	M.Reset();
	DCVoxelMesher::MeshPadded(Padded, Colors, 25.f, 0.f, M);
	TestEqual(TEXT("two materials: ten quads (shared face hidden)"), M.Indices.Num(), 10 * 6);

	// Faces on a chunk border hide against the neighbour chunk.
	W.Reset();
	W.Set(FIntVector(31, 0, 0), 1);
	W.Set(FIntVector(32, 0, 0), 1);
	W.CopyPadded(FIntVector(0, 0, 0), Padded);
	M.Reset();
	DCVoxelMesher::MeshPadded(Padded, Colors, 25.f, 0.f, M);
	TestEqual(TEXT("border voxel: five quads in its own chunk"), M.Indices.Num(), 5 * 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelModelTest, "Epic_AI_Mega_Jam2.Voxel.ForgeModels", VoxelFlags)
bool FDCVoxelModelTest::RunTest(const FString&)
{
	// Every shipped voxel-forge model parses, uses palette names the game knows, and every enemy model exists (R17).
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TArray<FString> Files;
	const FString Dir = FPaths::ProjectContentDir() / TEXT("DC/Voxel");
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.dcvox")), true, false);
	TestTrue(TEXT("models shipped"), Files.Num() >= 3);
	for (const FString& File : Files)
	{
		FString Text, Error;
		FDCVoxelModel M;
		TestTrue(FString::Printf(TEXT("%s loads"), *File), FFileHelper::LoadFileToString(Text, *(Dir / File)));
		TestTrue(FString::Printf(TEXT("%s parses: %s"), *File, *Error), FDCVoxelModel::Parse(Text, M, Error));
		for (const FName& Slot : M.Slots)
		{
			TestTrue(FString::Printf(TEXT("%s slot %s in palette"), *File, *Slot.ToString()), V.Palette.ContainsByPredicate([&Slot](const FDCVoxelMaterial& P) { return P.Id == Slot; }));
		}
	}
	for (const FDCEnemyDef& E : UDCTuning::Get().Enemies)
	{
		if (!E.Model.IsNone())
		{
			TestTrue(FString::Printf(TEXT("enemy %s model %s exists"), *E.Id.ToString(), *E.Model.ToString()), Files.Contains(E.Model.ToString() + TEXT(".dcvox")));
		}
	}
	// Malformed data is rejected, never trusted.
	FDCVoxelModel Bad;
	FString Error;
	auto Doc = [](std::initializer_list<const TCHAR*> Lines)
	{
		FString Out;
		for (const TCHAR* L : Lines)
		{
			Out += L;
			Out += TEXT("\n");
		}
		return Out;
	};
	TestFalse(TEXT("short volume rejected"), FDCVoxelModel::Parse(Doc({TEXT("dcvox 1"), TEXT("size 2 2 2"), TEXT("slots Ink"), TEXT("rle"), TEXT("3*1")}), Bad, Error));
	TestFalse(TEXT("unknown slot rejected"), FDCVoxelModel::Parse(Doc({TEXT("dcvox 1"), TEXT("size 1 1 1"), TEXT("slots Ink"), TEXT("rle"), TEXT("1*5")}), Bad, Error));
	TestTrue(TEXT("tiny model parses"), FDCVoxelModel::Parse(Doc({TEXT("dcvox 1"), TEXT("size 1 1 2"), TEXT("slots Ink"), TEXT("rle"), TEXT("1*0 1*1")}), Bad, Error) && Bad.Cells[1] == 1);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelFragmentTest, "Epic_AI_Mega_Jam2.Voxel.PhysicsFragments", VoxelFlags)
bool FDCVoxelFragmentTest::RunTest(const FString&)
{
	// A hollow 40x40x60 shell (a tower top): every cell lands in exactly one fragment, the cap holds, and the split
	// never depends on the order of the piece's cells.
	TArray<FIntVector> Cells;
	for (int32 Z = 0; Z < 60; ++Z)
	{
		for (int32 Y = 0; Y < 40; ++Y)
		{
			for (int32 X = 0; X < 40; ++X)
			{
				if (X == 0 || Y == 0 || X == 39 || Y == 39 || Z % 30 == 0)
				{
					Cells.Add(FIntVector(X, Y, Z));
				}
			}
		}
	}
	const TArray<TArray<int32>> Frags = DCVoxelOps::SplitFragments(Cells, 10, 24);
	TestTrue(TEXT("at most the cap"), Frags.Num() > 1 && Frags.Num() <= 24);
	TArray<int32> Seen;
	Seen.Init(0, Cells.Num());
	for (const TArray<int32>& F : Frags)
	{
		for (int32 i : F)
		{
			++Seen[i];
		}
	}
	bool bOnce = true;
	for (int32 N : Seen)
	{
		bOnce &= N == 1;
	}
	TestTrue(TEXT("every cell in exactly one fragment"), bOnce);

	TArray<FIntVector> Reversed = Cells;
	Algo::Reverse(Reversed);
	const TArray<TArray<int32>> Again = DCVoxelOps::SplitFragments(Reversed, 10, 24);
	bool bSame = Again.Num() == Frags.Num();
	for (int32 f = 0; bSame && f < Frags.Num(); ++f)
	{
		TSet<FIntVector> A, B;
		for (int32 i : Frags[f]) { A.Add(Cells[i]); }
		for (int32 i : Again[f]) { B.Add(Reversed[i]); }
		bSame = A.Num() == B.Num() && A.Includes(B);
	}
	TestTrue(TEXT("same fragments whatever the cell order"), bSame);
	TestEqual(TEXT("one cell, one fragment"), DCVoxelOps::SplitFragments({FIntVector(5)}, 10, 24).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelRestTest, "Epic_AI_Mega_Jam2.Voxel.HostRestOp", VoxelFlags)
bool FDCVoxelRestTest::RunTest(const FString&)
{
	// R26 host-owned rubble: the toppled piece's provisional rubble is replaced by its fragments settled at the poses
	// the host sends (even poses high in the air: Settle stacks them down), the same result on every machine.
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	FDCVoxelSector S;
	Craft(S, VoxelPlan(DCCityRules::SectorSeed(77, 0), 0));
	const FDCVoxelOpResult R = DCVoxelOps::Apply(S, StrikeAt(S.Towers[0], 1, 1300), Ctx);
	const FDCVoxelPiece* Top = R.Pieces.FindByPredicate([](const FDCVoxelPiece& P) { return P.bTopple; });
	TestNotNull(TEXT("a toppled piece"), Top);
	if (!Top)
	{
		return true;
	}
	FDCPendingPiece P;
	P.Lot = Top->Lot;
	P.Cells = Top->Cells;
	P.Materials = Top->Materials;
	for (int32 r = Top->RubbleStart; r < Top->RubbleStart + Top->RubbleNum; ++r)
	{
		P.Rubble.Add(R.Rubble[r]);
		P.RubbleMaterials.Add(R.RubbleMaterials[r]);
	}
	DCVoxelOps::FragmentTree(P.Cells, S.Towers[P.Lot].Grid.VoxelCm, Tn.PhysicsFragmentCm, Tn.PhysicsFragments, Tn.PhysicsLeafCm, Tn.PhysicsLeavesPerChunk, P.FragParent, P.FragCells);
	TestTrue(TEXT("the piece has a fragment tree"), P.FragParent.Num() > 1);
	// Rest every chunk 25 m off the provisional site, some on their side, some 20 m up.
	FDCVoxelOp Rest;
	Rest.Seq = 2;
	Rest.Kind = TEXT("Rest");
	Rest.RestOf = 1;
	Rest.RestPiece = 0;
	const FIntVector Site = P.Rubble.Num() > 0 ? S.Terrain.CenterCm(P.Rubble[0]) : FIntVector::ZeroValue;
	for (int32 f = 0; f < P.FragParent.Num(); ++f)
	{
		if (P.FragParent[f] != INDEX_NONE)
		{
			continue;
		}
		FDCVoxelRest& X = Rest.Rests.AddDefaulted_GetRef();
		X.Frag = f;
		X.PosCm = Site + FIntVector(2500 + f * 150, f * 80, (f % 3) * 1000);
		X.AxisX = static_cast<uint8>(f % 2 == 0 ? 0 : 4);
		X.AxisZ = static_cast<uint8>(f % 2 == 0 ? 4 : 1);
	}
	FDCVoxelSector Copy;
	Craft(Copy, VoxelPlan(DCCityRules::SectorSeed(77, 0), 0));
	DCVoxelOps::Apply(Copy, StrikeAt(Copy.Towers[0], 1, 1300), Ctx);
	const FDCVoxelOpResult A = DCVoxelOps::ApplyRest(S, Rest, P, Ctx);
	const FDCVoxelOpResult B = DCVoxelOps::ApplyRest(Copy, Rest, P, Ctx);
	TestTrue(TEXT("the rested fragments became rubble"), A.Rubble.Num() > 50);
	TestEqual(TEXT("every machine gets the same rubble"), S.Hash(), Copy.Hash());
	int32 Moved = 0;
	for (const FIntVector& C : A.Rubble)
	{
		Moved += S.Terrain.CenterCm(C).X > Site.X + 1500 ? 1 : 0;
	}
	TestTrue(TEXT("the rubble lies where the host's bodies rested"), Moved > A.Rubble.Num() / 2);
	int32 Floating = 0;
	for (const FIntVector& C : A.Rubble)
	{
		const bool bOnTower = S.Towers.ContainsByPredicate([&S, &C](const FDCVoxelTower& T) { return DCVoxelOps::TowerTopCells(T, S.Terrain, C.X, C.Y) == C.Z; });
		Floating += S.Terrain.Data.IsSolid(C - FIntVector(0, 0, 1)) || bOnTower ? 0 : 1;
	}
	TestEqual(TEXT("poses in the air still land (nothing floats)"), Floating, 0);
	TestEqual(TEXT("axis frames round trip"), static_cast<int32>(DCVoxelOps::AxisIndex(FVector(DCVoxelOps::AxisDir(3)))), 3);
	return true;
}

#endif
