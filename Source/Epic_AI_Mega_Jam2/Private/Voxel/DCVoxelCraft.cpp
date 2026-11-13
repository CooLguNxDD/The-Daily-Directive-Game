#include "Voxel/DCVoxelCraft.h"

#include "Voxel/DCVoxelWorld.h"
#include "Async/ParallelFor.h"

uint8 FDCVoxelCraftContext::MaterialId(FName Id) const
{
	const int32 Index = Palette ? Palette->IndexOfByPredicate([Id](const FDCVoxelMaterial& M) { return M.Id == Id; }) : INDEX_NONE;
	return Index == INDEX_NONE ? 1 : static_cast<uint8>(FMath::Min(Index + 1, 255));
}

const FDCVoxelRecipe* FDCVoxelCraftContext::Recipe(int32 Index) const
{
	if (!Recipes || Recipes->Num() == 0)
	{
		return nullptr;
	}
	return &(*Recipes)[FMath::Clamp(Index, 0, Recipes->Num() - 1)];
}

namespace
{
	/** What a facade cell becomes: solid wall, a recessed window (outer voxel air, inner glass) or an opening. */
	enum class EOpening : uint8 { Solid, Window, Open };

	/** Facade bay registry: pattern char -> which cells of the bay open. O = offset across the bay, H = height in the
	 *  floor, B = bay size, Sill/Win from the recipe. Adding a bay kind is a new row here. */
	using FBayFn = EOpening (*)(int32 O, int32 H, int32 B, int32 Sill, int32 Win);
	const TMap<TCHAR, FBayFn>& Bays()
	{
		// Sizes are fractions of the bay, so a recipe keeps its proportions at any voxel size.
		static const TMap<TCHAR, FBayFn> Table = {
			{TEXT('P'), [](int32, int32, int32, int32, int32) { return EOpening::Solid; }},
			{TEXT('W'), [](int32 O, int32 H, int32 B, int32 Sill, int32 Win)
				{ return O >= B / 6 && O < B - B / 6 && H >= Sill && H < Sill + Win ? EOpening::Window : EOpening::Solid; }},
			{TEXT('S'), [](int32 O, int32 H, int32 B, int32, int32)
				{ return O >= B / 2 - B / 12 && O < B / 2 + B / 12 && H >= B / 10 && H < B - B / 10 ? EOpening::Window : EOpening::Solid; }},
			{TEXT('G'), [](int32 O, int32 H, int32 B, int32 Sill, int32 Win)
				{ const bool bCol = (O >= B / 6 && O < B / 2 - B / 15) || (O >= B / 2 + B / 15 && O < B - B / 6); return bCol && H >= Sill && H < Sill + Win ? EOpening::Window : EOpening::Solid; }},
			{TEXT('D'), [](int32 O, int32 H, int32 B, int32, int32)
				{ return O >= B / 4 && O < B - B / 4 && H >= 1 && H < B * 2 / 3 ? EOpening::Open : EOpening::Solid; }},
		};
		return Table;
	}

	struct FFaceFrame
	{
		FIntVector Min;
		FIntVector Max;
		/** Cell of a face at depth D (0 = outer layer, -1 = one voxel proud of the wall), along-face U, height Z. */
		FIntVector Cell(int32 Face, int32 D, int32 U, int32 Z) const
		{
			// Faces 0/1 face -X/+X, faces 2/3 face -Y/+Y.
			const int32 NAxis = Face < 2 ? 0 : 1;
			const int32 UAxis = 1 - NAxis;
			FIntVector P(0, 0, Z);
			P[NAxis] = (Face % 2 == 0) ? Min[NAxis] + D : Max[NAxis] - 1 - D;
			P[UAxis] = Min[UAxis] + U;
			return P;
		}
		int32 Length(int32 Face) const { return Face < 2 ? Max.Y - Min.Y : Max.X - Min.X; }
	};

	struct FCraftIds
	{
		uint8 Wall = 1, Window = 1, Trim = 1, Slab = 1, Poster = 1, Ink = 1;
		/** About 25 cm in voxels: the roof and detail builders size their parts in units. */
		int32 Unit = 1;
	};

	void RingLayer(FDCVoxelWorld& W, const FFaceFrame& F, int32 Z, int32 Depth, uint8 Mat)
	{
		for (int32 Face = 0; Face < 4; ++Face)
		{
			for (int32 U = -1; U <= F.Length(Face); ++U)
			{
				W.Set(F.Cell(Face, Depth, U, Z), Mat);
			}
		}
	}

	/** Roof registry: builds above TopZ inside the top floor's box, returns the highest z written + 1. */
	using FRoofFn = int32 (*)(FDCVoxelWorld&, const FFaceFrame&, int32 TopZ, const FCraftIds&, FRandomStream&);

	int32 Parapet(FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids)
	{
		const int32 U = Ids.Unit;
		for (int32 Z = TopZ + 1; Z < TopZ + 1 + 3 * U; ++Z)
		{
			RingLayer(W, F, Z, 0, Ids.Wall);
		}
		for (int32 Z = TopZ + 1 + 3 * U; Z < TopZ + 1 + 4 * U; ++Z)
		{
			RingLayer(W, F, Z, 0, Ids.Trim);
		}
		return TopZ + 1 + 4 * U;
	}

	/** A mast Unit thick, Height units tall, with Bars cross bars. */
	int32 Mast(FDCVoxelWorld& W, const FIntVector& Base, int32 Height, int32 Bars, uint8 Mat, int32 U)
	{
		const int32 H = Height * U;
		W.FillBox(Base, Base + FIntVector(U, U, H), Mat);
		for (int32 B = 1; B <= Bars; ++B)
		{
			const int32 Z = Base.Z + H * B / (Bars + 1);
			W.FillBox(FIntVector(Base.X - 2 * U, Base.Y, Z), FIntVector(Base.X + 3 * U, Base.Y + U, Z + U), Mat);
		}
		return Base.Z + H;
	}

	const TMap<FName, FRoofFn>& Roofs()
	{
		static const TMap<FName, FRoofFn> Table = {
			{TEXT("Flat"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream& Rng)
				{
					const int32 U = Ids.Unit;
					const int32 Top = Parapet(W, F, TopZ, Ids);
					// A stair hut somewhere on the roof.
					const int32 Hut = 9 * U;
					const int32 X = Rng.RandRange(F.Min.X + 3 * U, FMath::Max(F.Min.X + 3 * U, F.Max.X - Hut - 3 * U));
					const int32 Y = Rng.RandRange(F.Min.Y + 3 * U, FMath::Max(F.Min.Y + 3 * U, F.Max.Y - Hut - 3 * U));
					W.FillBox(FIntVector(X, Y, TopZ + 1), FIntVector(X + Hut, Y + Hut, TopZ + 1 + Hut), Ids.Wall);
					W.FillBox(FIntVector(X - U, Y - U, TopZ + 1 + Hut), FIntVector(X + Hut + U, Y + Hut + U, TopZ + 1 + Hut + U), Ids.Trim);
					return FMath::Max(Top, TopZ + 1 + Hut + U);
				}},
			{TEXT("Crown"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					int32 Z = TopZ + 1;
					for (int32 Step = 1; Step <= 3; ++Step)
					{
						const FIntVector In(Step * 3 * U, Step * 3 * U, 0);
						if (F.Max.X - F.Min.X <= In.X * 2 + 2 * U || F.Max.Y - F.Min.Y <= In.Y * 2 + 2 * U)
						{
							break;
						}
						W.FillBox(FIntVector(F.Min.X, F.Min.Y, Z) + In, FIntVector(F.Max.X, F.Max.Y, Z + 4 * U) - In, Step == 3 ? Ids.Trim : Ids.Wall);
						Z += 4 * U;
					}
					const FIntVector Mid((F.Min.X + F.Max.X) / 2, (F.Min.Y + F.Max.Y) / 2, Z);
					return Mast(W, Mid, 16, 1, Ids.Trim, U);
				}},
			{TEXT("Spire"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					int32 Z = TopZ + 1;
					for (int32 Level = 1;; ++Level)
					{
						const int32 In = Level * 2 * U;
						if (F.Max.X - F.Min.X <= In * 2 + U || F.Max.Y - F.Min.Y <= In * 2 + U)
						{
							break;
						}
						W.FillBox(FIntVector(F.Min.X + In, F.Min.Y + In, Z), FIntVector(F.Max.X - In, F.Max.Y - In, Z + 3 * U), Level % 3 == 0 ? Ids.Trim : Ids.Wall);
						Z += 3 * U;
					}
					const FIntVector Mid((F.Min.X + F.Max.X) / 2, (F.Min.Y + F.Max.Y) / 2, Z);
					return Mast(W, Mid, 12, 0, Ids.Trim, U);
				}},
			{TEXT("Antenna"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream& Rng)
				{
					const int32 U = Ids.Unit;
					const int32 Top = Parapet(W, F, TopZ, Ids);
					const FIntVector Base(Rng.RandRange(F.Min.X + 4 * U, F.Max.X - 5 * U), Rng.RandRange(F.Min.Y + 4 * U, F.Max.Y - 5 * U), TopZ + 1);
					return FMath::Max(Top, Mast(W, Base, 30, 2, Ids.Trim, U));
				}},
		};
		return Table;
	}

	void Poster(FDCVoxelWorld& W, const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FCraftIds& Ids, FRandomStream& Rng)
	{
		// A paper poster one voxel proud of the wall, two floors tall, with an ink diamond: propaganda without red.
		const int32 Len = F.Length(Face);
		const int32 Width = FMath::Min(B * 2 - 2, Len - 4);
		if (Width < 6 || Floors < 4)
		{
			return;
		}
		const int32 U0 = Rng.RandRange(2, FMath::Max(2, Len - Width - 2));
		const int32 Z0 = B + 2;
		const int32 Height = FMath::Min(B * 2 - 2, (Floors - 2) * B);
		const int32 Cu = U0 + Width / 2;
		const int32 Cz = Z0 + Height / 2;
		const int32 R = FMath::Min(Width, Height) / 3;
		for (int32 U = U0; U < U0 + Width; ++U)
		{
			for (int32 Z = Z0; Z < Z0 + Height; ++Z)
			{
				const bool bEmblem = FMath::Abs(U - Cu) + FMath::Abs(Z - Cz) <= R;
				W.Set(F.Cell(Face, -1, U, Z), bEmblem ? Ids.Ink : Ids.Poster);
			}
		}
	}
}

namespace DCVoxelCraft
{
	void ApplyRecipes(FDCCityPlan& Plan, const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, int32 MaxFitBays)
	{
		if (Recipes.Num() == 0)
		{
			return;
		}
		TArray<TPair<int32, float>> Weighted;
		float Total = 0.f;
		for (const FDCVoxelRecipeWeight& W : Layout.Recipes)
		{
			const int32 Index = Recipes.IndexOfByPredicate([&W](const FDCVoxelRecipe& R) { return R.Id == W.Recipe; });
			if (Index != INDEX_NONE && W.Weight > 0.f)
			{
				Weighted.Add({Index, W.Weight});
				Total += W.Weight;
			}
		}
		if (Weighted.Num() == 0)
		{
			Weighted.Add({0, 1.f});
			Total = 1.f;
		}
		for (FDCLotPlan& Lot : Plan.Buildings)
		{
			FRandomStream Rng(Lot.Spec.Seed ^ 0x7EC1BE);
			float Pick = Rng.FRand() * Total;
			int32 Chosen = Weighted.Last().Key;
			for (const TPair<int32, float>& W : Weighted)
			{
				if (Pick < W.Value)
				{
					Chosen = W.Key;
					break;
				}
				Pick -= W.Value;
			}
			const FDCVoxelRecipe& R = Recipes[Chosen];
			Lot.Spec.Recipe = static_cast<uint8>(Chosen);
			const int32 Fit = FMath::Max(2, MaxFitBays);
			Lot.Spec.SizeX = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, Fit);
			Lot.Spec.SizeY = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, Fit);
			Lot.Spec.Floors = FMath::Max(2, Rng.RandRange(R.MinFloors, R.MaxFloors));
		}
	}

	void CraftTower(FDCVoxelTower& Tower, const FDCLotPlan& Lot, int32 LotIndex, const FDCVoxelCraftContext& Ctx)
	{
		Tower.Lot = LotIndex;
		const FDCVoxelRecipe* R = Ctx.Recipe(Lot.Spec.Recipe);
		if (!R || !Ctx.Tuning)
		{
			return;
		}
		const int32 Vcm = FMath::Max(1, Ctx.Tuning->TowerVoxelCm);
		const int32 B = FMath::Max(6, Ctx.BayCm / Vcm);
		const int32 Margin = FMath::Max(3, 60 / Vcm); // room for trim, posters and the bite's outer edge
		Tower.Grid.Data.Reset();
		Tower.Grid.VoxelCm = Vcm;
		const FIntVector Center = DCVoxelMath::ToCm(Lot.Location);
		const FIntVector Foot(Lot.Spec.SizeX * B, Lot.Spec.SizeY * B, Lot.Spec.Floors * B);
		Tower.Grid.OriginCm = FIntVector(Center.X - Foot.X * Vcm / 2 - Margin * Vcm, Center.Y - Foot.Y * Vcm / 2 - Margin * Vcm, 0);
		Tower.Min = FIntVector(Margin, Margin, 0);
		Tower.Max = Tower.Min + Foot;
		FDCVoxelWorld& World = Tower.Grid.Data;

		FCraftIds Ids;
		Ids.Wall = Ctx.MaterialId(R->Wall);
		Ids.Window = Ctx.MaterialId(R->Window);
		Ids.Trim = Ctx.MaterialId(R->Trim);
		Ids.Slab = Ctx.MaterialId(R->Slab);
		Ids.Poster = Ctx.MaterialId(R->Poster);
		Ids.Ink = Ctx.MaterialId(TEXT("Ink"));
		Ids.Unit = FMath::Max(1, 25 / Vcm);
		FRandomStream Rng(Lot.Spec.Seed ^ 0xC2AF7);
		const int32 Wall = FMath::Clamp(R->WallCm / Vcm, 1, B / 3);
		const int32 Sill = R->WindowSillCm / Vcm;
		const int32 Win = FMath::Max(1, R->WindowHeightCm / Vcm);
		const int32 Setback = R->SetbackCm / Vcm;
		const FString Upper = R->Facade.IsEmpty() ? FString(TEXT("W")) : R->Facade;
		const FString Ground = R->Base.IsEmpty() ? Upper : R->Base;
		const FBayFn Solid = Bays()[TEXT('P')];

		FFaceFrame Frame{Tower.Min, Tower.Max};
		int32 Top = 0;
		for (int32 Floor = 0; Floor < Lot.Spec.Floors; ++Floor)
		{
			const int32 Inset = R->SetbackEvery > 0 ? (Floor / R->SetbackEvery) * Setback : 0;
			const FIntVector In(Inset, Inset, 0);
			FFaceFrame F{Tower.Min + In, FIntVector(Tower.Max.X, Tower.Max.Y, 0) - In};
			if (F.Max.X - F.Min.X < B * 2 || F.Max.Y - F.Min.Y < B * 2)
			{
				F = Frame; // never step in past two bays
			}
			Frame = F;
			const int32 Z0 = Floor * B;
			// Floor slab (structure), then the walls of this floor.
			World.FillBox(FIntVector(F.Min.X, F.Min.Y, Z0), FIntVector(F.Max.X, F.Max.Y, Z0 + 1), Ids.Slab);
			const FString& Pattern = Floor == 0 ? Ground : Upper;
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const int32 Len = F.Length(Face);
				for (int32 U = 0; U < Len; ++U)
				{
					const bool bCorner = U < Wall || U >= Len - Wall;
					const FBayFn* Fn = Bays().Find(Pattern[(U / B) % Pattern.Len()]);
					const FBayFn Bay = Fn ? *Fn : Solid;
					for (int32 H = 0; H < B; ++H)
					{
						const EOpening Open = bCorner || H == 0 ? EOpening::Solid : Bay(U % B, H, B, Sill, Win);
						for (int32 D = 0; D < Wall; ++D)
						{
							const uint8 Outer = Open == EOpening::Solid ? Ids.Wall : 0;
							const uint8 Inner = Open == EOpening::Window ? Ids.Window : Outer;
							World.Set(F.Cell(Face, D, U, Z0 + H), D == 0 ? Outer : Inner);
						}
						// Window sill: a trim ledge one voxel proud under each opening.
						if (!bCorner && H > 0 && H + 1 == Sill && Bay(U % B, Sill, B, Sill, Win) == EOpening::Window)
						{
							World.Set(F.Cell(Face, -1, U, Z0 + H), Ids.Trim);
						}
					}
				}
			}
			if (Floor == 0 || (R->BandEvery > 0 && Floor % R->BandEvery == 0))
			{
				for (int32 Z = Z0; Z < Z0 + Ids.Unit; ++Z)
				{
					RingLayer(World, F, Z, -1, Ids.Trim); // plinth and string courses stand proud of the wall
				}
			}
			Top = Z0 + B;
		}
		// Roof slab, then the roof builder.
		World.FillBox(FIntVector(Frame.Min.X, Frame.Min.Y, Top), FIntVector(Frame.Max.X, Frame.Max.Y, Top + 1), Ids.Slab);
		RingLayer(World, Frame, Top, 0, Ids.Wall);
		const FRoofFn* Roof = Roofs().Find(R->Roof);
		const int32 RoofTop = (Roof ? *Roof : Roofs()[TEXT("Flat")])(World, Frame, Top, Ids, Rng);

		const FFaceFrame Full{Tower.Min, Tower.Max};
		for (int32 Face = 0; Face < 4; ++Face)
		{
			if (Rng.FRand() < R->PosterChance)
			{
				Poster(World, Full, Face, B, Lot.Spec.Floors, Ids, Rng);
			}
		}

		Tower.ScanMin = FIntVector(Tower.Min.X - Margin + 1, Tower.Min.Y - Margin + 1, 0);
		Tower.ScanMax = FIntVector(Tower.Max.X + Margin - 1, Tower.Max.Y + Margin - 1, FMath::Max(RoofTop, Top + 1) + 1);
		Tower.IntactLayers.SetNumZeroed(Tower.ScanMax.Z);
		for (int32 Z = 0; Z < Tower.ScanMax.Z; ++Z)
		{
			Tower.IntactLayers[Z] = CountLayer(World, Tower.ScanMin, Tower.ScanMax, Z);
		}
		Tower.bFallen = false;
		Tower.BreakZ = 0;
	}

	void CraftTerrain(FDCVoxelGrid& Out, const FDCCityPlan& Plan, float LotSizeCm, const FDCVoxelCraftContext& Ctx)
	{
		const FDCVoxelTuning& T = *Ctx.Tuning;
		const int32 Vcm = FMath::Max(1, T.TerrainVoxelCm);
		const int32 Layers = FMath::Max(1, T.GroundLayers);
		const int32 Half = FMath::RoundToInt32(Plan.HalfExtent) + T.TerrainMarginCm;
		const int32 N = 2 * Half / Vcm;
		Out.Data.Reset();
		Out.VoxelCm = Vcm;
		Out.OriginCm = FIntVector(-Half, -Half, -Layers * Vcm);
		const uint8 Street = Ctx.MaterialId(T.StreetMaterial);
		const uint8 Paving = Ctx.MaterialId(T.LotMaterial);
		const uint8 Kerb = Ctx.MaterialId(T.KerbMaterial);
		Out.Data.FillBox(FIntVector(0, 0, 0), FIntVector(N, N, Layers), Street);
		// Lane dashes down the middle of every street (flush paint in the top layer).
		const uint8 Paint = Ctx.MaterialId(TEXT("Paint"));
		TArray<int32> LinesX, LinesY;
		for (const FVector& P : Plan.StreetPoints)
		{
			LinesX.AddUnique(FMath::RoundToInt32(P.X));
			LinesY.AddUnique(FMath::RoundToInt32(P.Y));
		}
		const int32 DashOn = FMath::Max(1, 150 / Vcm);
		const int32 DashPeriod = FMath::Max(DashOn + 1, 450 / Vcm);
		for (int32 X : LinesX)
		{
			const int32 Lx = Out.ToLocal(FIntVector(X, 0, 0)).X;
			for (int32 Y = 0; Y < N; ++Y)
			{
				if (Y % DashPeriod < DashOn)
				{
					Out.Data.Set(FIntVector(Lx, Y, Layers - 1), Paint);
				}
			}
		}
		for (int32 Y : LinesY)
		{
			const int32 Ly = Out.ToLocal(FIntVector(0, Y, 0)).Y;
			for (int32 X = 0; X < N; ++X)
			{
				if (X % DashPeriod < DashOn)
				{
					Out.Data.Set(FIntVector(X, Ly, Layers - 1), Paint);
				}
			}
		}
		// Every built lot gets paving and a kerb ring one voxel above the street.
		const int32 LotHalf = FMath::RoundToInt32(LotSizeCm * 0.5f);
		const int32 Z = Layers - 1;
		for (const FDCLotPlan& Lot : Plan.Buildings)
		{
			const FIntVector C = DCVoxelMath::ToCm(Lot.Location);
			const FIntVector Lo = Out.ToLocal(FIntVector(C.X - LotHalf, C.Y - LotHalf, 0));
			const FIntVector Hi = Out.ToLocal(FIntVector(C.X + LotHalf, C.Y + LotHalf, 0));
			Out.Data.FillBox(FIntVector(Lo.X, Lo.Y, Z), FIntVector(Hi.X, Hi.Y, Z + 1), Paving);
			for (int32 X = Lo.X; X < Hi.X; ++X)
			{
				Out.Data.Set(FIntVector(X, Lo.Y, Z + 1), Kerb);
				Out.Data.Set(FIntVector(X, Hi.Y - 1, Z + 1), Kerb);
			}
			for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
			{
				Out.Data.Set(FIntVector(Lo.X, Y, Z + 1), Kerb);
				Out.Data.Set(FIntVector(Hi.X - 1, Y, Z + 1), Kerb);
			}
		}
	}

	void CraftCity(const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx, TArray<FDCVoxelTower>& OutTowers)
	{
		OutTowers.Reset();
		OutTowers.SetNum(Plan.Buildings.Num());
		// Towers are independent grids: craft them in parallel (each task writes only its own grid).
		ParallelFor(Plan.Buildings.Num(), [&](int32 Lot) { CraftTower(OutTowers[Lot], Plan.Buildings[Lot], Lot, Ctx); });
	}

	int32 CountLayer(const FDCVoxelWorld& World, const FIntVector& Min, const FIntVector& Max, int32 Z)
	{
		// Chunk-wise: one map lookup per chunk instead of per voxel.
		int32 N = 0;
		const int32 S = FDCVoxelChunk::Size;
		const FIntVector CMin = FDCVoxelWorld::ChunkOf(FIntVector(Min.X, Min.Y, Z));
		const FIntVector CMax = FDCVoxelWorld::ChunkOf(FIntVector(Max.X - 1, Max.Y - 1, Z));
		const int32 Lz = Z & FDCVoxelChunk::Mask;
		for (int32 Cy = CMin.Y; Cy <= CMax.Y; ++Cy)
		{
			for (int32 Cx = CMin.X; Cx <= CMax.X; ++Cx)
			{
				const FDCVoxelChunk* C = World.FindChunk(FIntVector(Cx, Cy, CMin.Z));
				if (!C || C->Solid == 0)
				{
					continue;
				}
				const int32 X0 = FMath::Max(Min.X, Cx * S) - Cx * S;
				const int32 X1 = FMath::Min(Max.X, Cx * S + S) - Cx * S;
				const int32 Y0 = FMath::Max(Min.Y, Cy * S) - Cy * S;
				const int32 Y1 = FMath::Min(Max.Y, Cy * S + S) - Cy * S;
				for (int32 Y = Y0; Y < Y1; ++Y)
				{
					for (int32 X = X0; X < X1; ++X)
					{
						N += C->V[FDCVoxelChunk::Index(X, Y, Lz)] != 0 ? 1 : 0;
					}
				}
			}
		}
		return N;
	}
}
