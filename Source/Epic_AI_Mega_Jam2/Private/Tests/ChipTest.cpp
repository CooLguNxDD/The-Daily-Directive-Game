// R24 weapon chips (advisor a-37): the Chip op is deterministic, folds, opens holes and can bring a tower down; the
// host's chip ledger keeps its window, budget, rate limit and structure interval. Pure: runs under -nullrhi with the
// shipped Config/DefaultGame.ini data.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCChipRules.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelOps.h"
#include "Voxel/DCVoxelSettings.h"
#include "Voxel/DCVoxelWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ChipFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	void CraftSector(FDCVoxelSector& S, int32 Seed, int32 Sector)
	{
		const UDCTuning& T = UDCTuning::Get();
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		FDCCityPlan Plan = DCCityRules::PlanCity(DCCityRules::SectorSeed(Seed, Sector), T.City, T.Look.BuildingColors.Num());
		DCVoxelCraft::ApplyRecipes(Plan, V.SectorLayout(Sector), V.Recipes, FMath::FloorToInt(T.City.LotSizeCm / T.City.BlockSizeCm));
		const FDCVoxelCraftContext Ctx = V.CraftContext(T.City.BlockSizeCm);
		DCVoxelCraft::CraftCity(Plan, Ctx, S.Towers);
		DCVoxelCraft::CraftTerrain(S.Terrain, Plan, T.City.LotSizeCm, Ctx);
	}

	/** Centres (sector cm) of solid voxels on a tower's -X wall at layer Z, along Y (what a shooter on the street hits). */
	TArray<FIntVector> WallVoxels(const FDCVoxelTower& T, int32 Z)
	{
		TArray<FIntVector> Out;
		for (int32 Y = T.Min.Y; Y < T.Max.Y; ++Y)
		{
			for (int32 X = T.Min.X; X < T.Max.X; ++X)
			{
				if (T.Grid.Data.IsSolid(FIntVector(X, Y, Z)))
				{
					Out.Add(T.Grid.CenterCm(FIntVector(X, Y, Z)));
					break;
				}
			}
		}
		return Out;
	}

	FDCVoxelOp ChipOp(int32 Seq, int32 Lot, const TArray<FIntVector>& Centers, int32 RadiusCm)
	{
		FDCVoxelOp Op;
		Op.Seq = Seq;
		Op.Kind = TEXT("Chip");
		for (const FIntVector& C : Centers)
		{
			FDCVoxelChip& Chip = Op.Chips.AddDefaulted_GetRef();
			Chip.Grid = Lot;
			Chip.CenterCm = C;
			Chip.RadiusCm = RadiusCm;
		}
		Op.CenterCm = Centers.Num() > 0 ? Centers[0] : FIntVector::ZeroValue;
		return Op;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCChipOpTest, "Epic_AI_Mega_Jam2.Chips.OpDeterministicAndFolds", ChipFlags)
bool FDCChipOpTest::RunTest(const FString&)
{
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	FDCVoxelSector A, B, Folded;
	CraftSector(A, 4242, 0);
	CraftSector(B, 4242, 0);
	CraftSector(Folded, 4242, 0);
	TestTrue(TEXT("a city with towers"), A.Towers.Num() > 0);
	if (A.Towers.Num() == 0)
	{
		return false;
	}
	const FDCVoxelTower& T = A.Towers[0];
	const int32 SupportTop = Ctx.Tuning->ChipSupportFloors * FMath::Max(6, Ctx.BayCm / T.Grid.VoxelCm);
	const TArray<FIntVector> Wall = WallVoxels(T, FMath::Min(T.Max.Z - 2, SupportTop + 5)); // above the support band
	TestTrue(TEXT("the wall has voxels to shoot"), Wall.Num() > 8);
	TArray<FIntVector> First, Second;
	for (int32 i = 0; i < Wall.Num(); i += 3) // 30 cm apart: no chip's sphere reaches the next chip's voxel
	{
		(i < Wall.Num() / 2 ? First : Second).Add(Wall[i]);
	}
	const FDCVoxelOp Op1 = ChipOp(1, 0, First, 12);
	const FDCVoxelOp Op2 = ChipOp(2, 0, Second, 12);
	// Same Chip ops on two machines: same voxels.
	const uint32 Before = A.Hash();
	const FDCVoxelOpResult R1 = DCVoxelOps::Run(A, Op1, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(B, Op1, Ctx, FDCPendingPiece());
	TestEqual(TEXT("one chip op: both machines hold the same voxels"), A.Hash(), B.Hash());
	TestNotEqual(TEXT("the chips changed the tower"), A.Hash(), Before);
	TestTrue(TEXT("every chip carved"), R1.Carved >= First.Num());
	TestEqual(TEXT("one crumb cluster per chip that carved"), R1.ShardClusters.Num(), First.Num());
	TestEqual(TEXT("no shard bodies (raw shards stay empty)"), R1.Shards.Num(), 0);
	TestEqual(TEXT("no structure pass without StructureLots"), R1.Pieces.Num(), 0);
	int32 Credited = 0;
	for (const FIntPoint& C : R1.ChipCarved)
	{
		Credited += C.X == 0 ? C.Y : 0;
	}
	TestEqual(TEXT("chips above ChipSupportFloors are no support"), Credited, 0);
	DCVoxelOps::Run(A, Op2, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(B, Op2, Ctx, FDCPendingPiece());
	TestEqual(TEXT("two chip ops: both machines hold the same voxels"), A.Hash(), B.Hash());
	// Folded (the reel restage, a late joiner): both ops' spheres in one pass give the same voxels.
	FDCVoxelOp Fold = Op1;
	Fold.Chips.Append(Op2.Chips);
	TestTrue(TEXT("chip-only ops fold"), DCVoxelOps::IsChipOnly(Op1) && DCVoxelOps::IsChipOnly(Op2));
	DCVoxelOps::Run(Folded, Fold, Ctx, FDCPendingPiece());
	TestEqual(TEXT("folded chip ops leave the same voxels"), Folded.Hash(), A.Hash());
	// History merge (MergeInto): op 2 merges into op 1's entry. A client that applied op 1 already and then the merged
	// entry ends with the same voxels as one that applied them one by one.
	FDCVoxelOp Entry = Op1;
	FDCVoxelOp Next = Op2;
	TestTrue(TEXT("a chip-only op merges into a chip-only entry"), DCChipRules::MergeInto(Entry, Next, 1000));
	TestTrue(TEXT("the merged entry holds both and takes the newer seq"), Entry.Chips.Num() == Op1.Chips.Num() + Op2.Chips.Num() && Entry.Seq == Op2.Seq);
	FDCVoxelSector Lagging;
	CraftSector(Lagging, 4242, 0);
	DCVoxelOps::Run(Lagging, Op1, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(Lagging, Entry, Ctx, FDCPendingPiece());
	TestEqual(TEXT("a lagging client that re-applies the merged entry matches"), Lagging.Hash(), A.Hash());
	FDCVoxelOp Small = Op1;
	TestFalse(TEXT("no merge past MaxChips"), DCChipRules::MergeInto(Small, Op2, Op1.Chips.Num()));
	FDCVoxelOp Strike = Op1;
	Strike.Kind = TEXT("Strike");
	TestFalse(TEXT("no merge into a strike"), DCChipRules::MergeInto(Strike, Op2, 1000));
	FDCVoxelOp Struct = Op2;
	Struct.StructureLots.Add(0);
	FDCVoxelOp Base = Op1;
	TestFalse(TEXT("a structure op never merges"), DCChipRules::MergeInto(Base, Struct, 1000));
	FDCVoxelOp OtherBuild = Op2;
	OtherBuild.Build = Op1.Build + 1;
	TestFalse(TEXT("no merge across builds"), DCChipRules::MergeInto(Base, OtherBuild, 1000));
	// Holes: the chipped wall voxels are gone (you see and shoot through).
	TestFalse(TEXT("a chipped wall voxel is open"), T.Grid.Data.IsSolid(T.Grid.ToLocal(First[0])));
	// Kinds: the terrain flag drives the city's street snapshot.
	TestFalse(TEXT("a chip-only op never writes the street"), DCVoxelOps::WritesTerrain(Op1));
	FDCVoxelOp WithStructure = Op1;
	WithStructure.StructureLots.Add(0);
	TestTrue(TEXT("a chip op with a structure pass may write the street"), DCVoxelOps::WritesTerrain(WithStructure));
	TestFalse(TEXT("a chip op with a structure pass never folds"), DCVoxelOps::IsChipOnly(WithStructure));
	// Validation: an unknown grid or kind does nothing on every machine.
	FDCVoxelOp Bad = ChipOp(3, 9999, First, 12);
	const uint32 H = A.Hash();
	DCVoxelOps::Run(A, Bad, Ctx, FDCPendingPiece());
	Bad.Kind = TEXT("NoSuchKind");
	DCVoxelOps::Run(A, Bad, Ctx, FDCPendingPiece());
	TestEqual(TEXT("unknown grid and unknown kind change nothing"), A.Hash(), H);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCChipStructureTest, "Epic_AI_Mega_Jam2.Chips.ShotAwaySupportFalls", ChipFlags)
bool FDCChipStructureTest::RunTest(const FString&)
{
	// Chips low on a tower count as support; high ones do not. Listed in StructureLots, the tower's base gives way on
	// the shooters' side and the structure pass brings it down like a strike, the same on two machines.
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	FDCVoxelSector A, B;
	CraftSector(A, 77, 0);
	CraftSector(B, 77, 0);
	const int32 Lot = 0;
	FDCVoxelTower& T = A.Towers[Lot];
	const TArray<FIntVector> Low = WallVoxels(T, 4);
	const TArray<FIntVector> High = WallVoxels(T, T.Max.Z - 8);
	TestTrue(TEXT("walls to shoot low and high"), Low.Num() > 4 && High.Num() > 4);
	const int32 Mid = Low.Num() / 2;
	FDCVoxelOp Shots = ChipOp(1, Lot, {Low[Mid - 1], Low[Mid], Low[Mid + 1], High[High.Num() / 2]}, 12);
	const FDCVoxelOpResult R = DCVoxelOps::Run(A, Shots, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(B, Shots, Ctx, FDCPendingPiece());
	int32 Support = 0;
	for (const FIntPoint& C : R.ChipCarved)
	{
		Support += C.Y;
	}
	TestEqual(TEXT("only the low chips count as support"), R.ChipCarved.Num(), 3);
	TestTrue(TEXT("support voxels are fewer than all carved"), Support > 0 && Support < R.Carved);
	TestFalse(TEXT("chips alone never run the structure pass"), T.bFallen);
	TestEqual(TEXT("chips alone: no pieces"), R.Pieces.Num(), 0);
	FDCVoxelOp Bring = ChipOp(2, Lot, {}, 1);
	Bring.StructureLots.Add(Lot);
	Bring.CenterCm = Low[Mid]; // the shots came from the middle of the -X wall
	const FDCVoxelOpResult S = DCVoxelOps::Run(A, Bring, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(B, Bring, Ctx, FDCPendingPiece());
	TestTrue(TEXT("the shot-away tower fell"), S.FallenLots.Contains(Lot) && T.bFallen);
	TestTrue(TEXT("its top broke off as a piece and settled"), S.Pieces.Num() > 0 && S.Rubble.Num() > 0);
	TestEqual(TEXT("the structure pass is the same on both machines"), A.Hash(), B.Hash());
	int64 SumX = 0;
	for (const FIntVector& P : S.Rubble)
	{
		SumX += A.Terrain.CenterCm(P).X;
	}
	TestTrue(TEXT("it falls away from the shooters"), SumX / FMath::Max(1, S.Rubble.Num()) > (T.FootMinCm().X + T.FootMaxCm().X) / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCChipLedgerTest, "Epic_AI_Mega_Jam2.Chips.LedgerWindowAndRate", ChipFlags)
bool FDCChipLedgerTest::RunTest(const FString&)
{
	FDCChipParams P;
	P.WindowSeconds = 0.25f;
	P.MaxEntries = 3;
	P.MaxCellsPerOp = 2;
	P.RenderCellCm = 1000;
	P.MaxPending = 6;
	P.StructureVoxels = 50;
	P.StructureMinSeconds = 3.f;
	P.StructureMaxBodyLoad = 0.6f;
	FDCChipLedger L;
	// Damage: a bucket chips only past its HP, then starts over.
	TestFalse(TEXT("half the HP: no chip"), DCChipRules::AddHit(L, 0, FIntVector(5, 5, 105), 30, 0.5f, 1.f, 12, P.MaxPending));
	TestTrue(TEXT("the second half chips"), DCChipRules::AddHit(L, 0, FIntVector(15, 5, 105), 30, 0.5f, 1.f, 12, P.MaxPending));
	TestFalse(TEXT("the bucket started over"), DCChipRules::AddHit(L, 0, FIntVector(15, 5, 105), 30, 0.5f, 1.f, 12, P.MaxPending));
	TestFalse(TEXT("the same voxel again merges (no duplicate)"), DCChipRules::AddHit(L, 0, FIntVector(15, 5, 105), 30, 0.5f, 1.f, 20, P.MaxPending));
	TestEqual(TEXT("one pending chip, its radius the larger"), L.Pending.Num(), 1);
	TestEqual(TEXT("merged radius"), L.Pending.Num() > 0 ? L.Pending[0].RadiusCm : 0, 20);
	// Fill: chips on three render cells (cells of 1000 cm), more than MaxEntries in total.
	const FIntVector Spots[] = {FIntVector(105, 5, 5), FIntVector(205, 5, 5), FIntVector(1205, 5, 5), FIntVector(2205, 5, 5), FIntVector(305, 5, 5)};
	for (const FIntVector& S : Spots)
	{
		DCChipRules::AddHit(L, 0, S, 30, 1.f, 1.f, 12, P.MaxPending);
	}
	TestEqual(TEXT("six pending"), L.Pending.Num(), 6);
	TestFalse(TEXT("pending is capped"), DCChipRules::AddHit(L, 0, FIntVector(4005, 5, 5), 30, 1.f, 1.f, 12, P.MaxPending));
	// Window: nothing while a Chip op is out; then one op under the entry and cell budgets, the rest stays.
	FDCVoxelOp Op;
	TestFalse(TEXT("no op while one is out"), DCChipRules::Flush(L, 1.f, P, true, 0.f, Op));
	TestTrue(TEXT("a due window sends an op"), DCChipRules::Flush(L, 1.f, P, false, 0.f, Op));
	TestEqual(TEXT("the op is a Chip op"), Op.Kind, FName(TEXT("Chip")));
	TestEqual(TEXT("entries capped"), Op.Chips.Num(), 3);
	TArray<FIntVector> Cells;
	for (const FDCVoxelChip& C : Op.Chips)
	{
		Cells.AddUnique(FIntVector(C.CenterCm.X / 1000, C.CenterCm.Y / 1000, C.CenterCm.Z / 1000));
	}
	TestTrue(TEXT("render cells capped"), Cells.Num() <= 2);
	TestEqual(TEXT("op + remainder = pending before"), Op.Chips.Num() + L.Pending.Num(), 6);
	TestFalse(TEXT("the window is closed until WindowSeconds pass"), DCChipRules::Flush(L, 1.1f, P, false, 0.f, Op));
	TestTrue(TEXT("the next window sends the rest"), DCChipRules::Flush(L, 1.25f, P, false, 0.f, Op));
	// Rate limit: ChipsPerSecond hits a second per shooter (a burst of that many), refilled over time.
	int32 Taken = 0;
	for (int32 i = 0; i < 20; ++i)
	{
		Taken += DCChipRules::TakeToken(L, 7u, 4.f, 10.f) ? 1 : 0;
	}
	TestEqual(TEXT("a burst of PerSecond tokens"), Taken, 4);
	TestTrue(TEXT("another shooter has its own bucket"), DCChipRules::TakeToken(L, 8u, 4.f, 10.f));
	TestTrue(TEXT("refilled after a quarter second"), DCChipRules::TakeToken(L, 7u, 4.f, 10.25f));
	TestFalse(TEXT("a weapon with no chip rate never chips"), DCChipRules::TakeToken(L, 9u, 0.f, 10.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCChipTriggerTest, "Epic_AI_Mega_Jam2.Chips.StructureTriggerOncePerInterval", ChipFlags)
bool FDCChipTriggerTest::RunTest(const FString&)
{
	FDCChipParams P;
	P.WindowSeconds = 0.25f;
	P.StructureVoxels = 50;
	P.StructureMinSeconds = 3.f;
	P.StructureMaxBodyLoad = 0.6f;
	FDCChipLedger L;
	FDCVoxelOp Op;
	// Lot 4 chipped from the -X side.
	DCChipRules::AddHit(L, 4, FIntVector(-100, 0, 300), 30, 1.f, 1.f, 12, P.MaxPending);
	TestTrue(TEXT("first window"), DCChipRules::Flush(L, 0.f, P, false, 0.f, Op));
	TestEqual(TEXT("under the voxel threshold: no structure"), Op.StructureLots.Num(), 0);
	DCChipRules::Credit(L, 4, 60);
	DCChipRules::Credit(L, 2, 10);
	TestFalse(TEXT("over the threshold but bodies over the gate: waits"), DCChipRules::Flush(L, 1.f, P, false, 0.9f, Op));
	TestTrue(TEXT("over the threshold under the gate: structure op"), DCChipRules::Flush(L, 1.f, P, false, 0.f, Op));
	TestEqual(TEXT("the lot is listed"), Op.StructureLots.Num() == 1 ? Op.StructureLots[0] : -1, 4);
	TestEqual(TEXT("the structure centre is where its chips came from"), Op.CenterCm, FIntVector(-100, 0, 300));
	DCChipRules::Credit(L, 4, 200);
	TestFalse(TEXT("again inside the interval: no structure op"), DCChipRules::Flush(L, 2.f, P, false, 0.f, Op));
	TestTrue(TEXT("past the interval: structure again"), DCChipRules::Flush(L, 4.1f, P, false, 0.f, Op));
	TestEqual(TEXT("once per interval"), Op.StructureLots.Num(), 1);
	TestFalse(TEXT("the count started over"), DCChipRules::Flush(L, 8.f, P, false, 0.f, Op));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCChipPropBreakTest, "Epic_AI_Mega_Jam2.Chips.PropBreaksAtThreshold", ChipFlags)
bool FDCChipPropBreakTest::RunTest(const FString&)
{
	const FDCVoxelOpContext Ctx = UDCVoxelSettings::Get().OpContext(UDCTuning::Get().City.BlockSizeCm);
	// Ledger: a prop of 400 voxels that breaks at 35 % (140): chips below that wear it, the one past it lists it once.
	FDCChipLedger L;
	FDCChipProp& Lamp = L.Props.AddDefaulted_GetRef();
	Lamp.FootCm = FIntVector(100, 200, 0);
	Lamp.RadiusCm = 20;
	Lamp.TopCm = 200;
	Lamp.BreakAt = 140;
	FDCChipProp& Bench = L.Props.AddDefaulted_GetRef();
	Bench.FootCm = FIntVector(600, 200, 0);
	Bench.RadiusCm = 40;
	Bench.TopCm = 60;
	Bench.BreakAt = 0; // never breaks
	TestEqual(TEXT("a chip on the lamp finds the lamp"), DCChipRules::PropAt(L, FIntVector(110, 205, 150)), 0);
	TestEqual(TEXT("a chip over the lamp's top finds nothing"), DCChipRules::PropAt(L, FIntVector(110, 205, 400)), INDEX_NONE);
	TestEqual(TEXT("a chip on the bench finds the bench"), DCChipRules::PropAt(L, FIntVector(610, 190, 30)), 1);
	DCChipRules::CreditProp(L, 0, 100);
	TestEqual(TEXT("under the threshold: not due"), L.BreakDue.Num(), 0);
	DCChipRules::CreditProp(L, 0, 50);
	TestEqual(TEXT("past the threshold: due"), L.BreakDue.Num(), 1);
	DCChipRules::CreditProp(L, 0, 50);
	DCChipRules::CreditProp(L, 1, 5000);
	TestEqual(TEXT("due once, and a prop with no break percent never breaks"), L.BreakDue.Num(), 1);
	FDCChipParams P;
	FDCVoxelOp Op;
	TestTrue(TEXT("a due break sends an op"), DCChipRules::Flush(L, 0.f, P, false, 0.f, Op));
	TestTrue(TEXT("the op breaks the lamp: its foot, top and reach"), Op.BreakProps.Num() == 1 && Op.BreakProps[0].CenterCm == FIntVector(100, 200, 200) && Op.BreakProps[0].RadiusCm == 20);
	TestEqual(TEXT("a broken prop takes no more chips"), DCChipRules::PropAt(L, FIntVector(110, 205, 150)), INDEX_NONE);

	// Op: the column clears the prop (and nothing past its top or reach) on every machine alike, in pieces.
	auto Grid = [](FDCVoxelSector& S)
	{
		S.Props.VoxelCm = 5;
		S.Props.OriginCm = FIntVector(-1000, -1000, 0);
		const FIntVector Foot = S.Props.ToLocal(FIntVector(100, 200, 0));
		S.Props.Data.FillBox(Foot - FIntVector(2, 2, 0), Foot + FIntVector(2, 2, 40), 1); // the lamp: 20 cm square, 2 m
		S.Props.Data.FillBox(Foot + FIntVector(-1, -1, 60), Foot + FIntVector(1, 1, 62), 2); // a cable above its top
	};
	FDCVoxelSector A, B;
	Grid(A);
	Grid(B);
	const FDCVoxelOpResult R = DCVoxelOps::Run(A, Op, Ctx, FDCPendingPiece());
	DCVoxelOps::Run(B, Op, Ctx, FDCPendingPiece());
	TestEqual(TEXT("both machines break it the same"), A.Hash(), B.Hash());
	TestEqual(TEXT("the lamp is gone"), R.Carved, 16 * 40);
	TestTrue(TEXT("the cable over it stays"), A.Props.Data.CountSolid() == 4 * 2);
	TestTrue(TEXT("it flies apart as pieces"), R.ShardClusters.Num() == 6 && R.ShardClusters[0].bPiece);
	return true;
}
#endif
