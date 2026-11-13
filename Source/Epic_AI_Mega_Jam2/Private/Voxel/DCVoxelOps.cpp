#include "Voxel/DCVoxelOps.h"

#include "Voxel/DCVoxelWorld.h"
#include "Async/ParallelFor.h"

const FDCVoxelCarveDef& FDCVoxelOpContext::CarveFor(FName Stratagem) const
{
	const FDCVoxelCarveDef* Found = Carves ? Carves->FindByPredicate([Stratagem](const FDCVoxelCarveDef& C) { return C.Stratagem == Stratagem; }) : nullptr;
	return Found ? *Found : Tuning->DefaultCarve;
}

uint32 FDCVoxelSector::Hash() const
{
	uint32 H = HashCombine(Terrain.Data.ContentHash(), Props.Data.ContentHash());
	for (const FDCVoxelTower& T : Towers)
	{
		H = HashCombine(H, T.Grid.Data.ContentHash());
	}
	return H;
}

namespace
{
	/** A flat copy of a box of a grid (x fastest), read chunk by chunk. */
	struct FBox3
	{
		FIntVector Min;
		FIntVector Size;
		TArray<uint8> V;

		int32 Index(int32 X, int32 Y, int32 Z) const { return X + Size.X * (Y + Size.Y * Z); }
		bool Inside(int32 X, int32 Y, int32 Z) const { return X >= 0 && Y >= 0 && Z >= 0 && X < Size.X && Y < Size.Y && Z < Size.Z; }

		void Read(const FDCVoxelWorld& World, const FIntVector& InMin, const FIntVector& InMax)
		{
			Min = InMin;
			Size = InMax - InMin;
			V.SetNumZeroed(Size.X * Size.Y * Size.Z);
			const int32 S = FDCVoxelChunk::Size;
			const FIntVector C0 = FDCVoxelWorld::ChunkOf(InMin);
			const FIntVector C1 = FDCVoxelWorld::ChunkOf(InMax - FIntVector(1));
			for (int32 Cz = C0.Z; Cz <= C1.Z; ++Cz)
			{
				for (int32 Cy = C0.Y; Cy <= C1.Y; ++Cy)
				{
					for (int32 Cx = C0.X; Cx <= C1.X; ++Cx)
					{
						const FDCVoxelChunk* C = World.FindChunk(FIntVector(Cx, Cy, Cz));
						if (!C || C->Solid == 0)
						{
							continue;
						}
						const FIntVector Base(Cx * S, Cy * S, Cz * S);
						const FIntVector Lo(FMath::Max(InMin.X, Base.X), FMath::Max(InMin.Y, Base.Y), FMath::Max(InMin.Z, Base.Z));
						const FIntVector Hi(FMath::Min(InMax.X, Base.X + S), FMath::Min(InMax.Y, Base.Y + S), FMath::Min(InMax.Z, Base.Z + S));
						for (int32 Z = Lo.Z; Z < Hi.Z; ++Z)
						{
							for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
							{
								for (int32 X = Lo.X; X < Hi.X; ++X)
								{
									V[Index(X - InMin.X, Y - InMin.Y, Z - InMin.Z)] = C->V[FDCVoxelChunk::Index(X - Base.X, Y - Base.Y, Z - Base.Z)];
								}
							}
						}
					}
				}
			}
		}
	};

	/** Axis direction from A toward B on the dominant axis (+X on a tie). */
	FIntVector DominantDir(const FIntVector& From, const FIntVector& To)
	{
		const int32 Dx = To.X - From.X;
		const int32 Dy = To.Y - From.Y;
		if (FMath::Abs(Dy) > FMath::Abs(Dx))
		{
			return FIntVector(0, Dy > 0 ? 1 : -1, 0);
		}
		return FIntVector(Dx < 0 ? -1 : 1, 0, 0);
	}

	FIntVector TowerCenterCm(const FDCVoxelTower& T)
	{
		const FIntVector A = T.FootMinCm();
		const FIntVector B = T.FootMaxCm();
		return FIntVector((A.X + B.X) / 2, (A.Y + B.Y) / 2, 0);
	}

	/** Squared 2D distance (cm) from P to the tower footprint (0 inside). */
	int64 DistSqToFootprint(const FDCVoxelTower& T, const FIntVector& P)
	{
		const FIntVector A = T.FootMinCm();
		const FIntVector B = T.FootMaxCm();
		const int64 Dx = FMath::Max3(A.X - P.X, 0, P.X - B.X);
		const int64 Dy = FMath::Max3(A.Y - P.Y, 0, P.Y - B.Y);
		return Dx * Dx + Dy * Dy;
	}

	bool InFootprint(const FDCVoxelTower& T, int32 Xcm, int32 Ycm)
	{
		const FIntVector A = T.FootMinCm();
		const FIntVector B = T.FootMaxCm();
		return Xcm >= A.X && Xcm < B.X && Ycm >= A.Y && Ycm < B.Y;
	}

	constexpr int32 MaxSlides = 24;

	/** Top (highest solid z + 1) of a terrain column, counting standing towers and stumps as blockers. Cached per op. */
	struct FColumnTops
	{
		const FDCVoxelSector& Sector;
		int32 Ceiling = 0;
		TMap<FIntPoint, int32> Tops;

		int32 Get(const FIntPoint& C)
		{
			if (const int32* Found = Tops.Find(C))
			{
				return *Found;
			}
			const FDCVoxelGrid& T = Sector.Terrain;
			int32 Top = 0;
			for (int32 Z = Ceiling; Z >= 0; --Z)
			{
				if (T.Data.IsSolid(FIntVector(C.X, C.Y, Z)))
				{
					Top = Z + 1;
					break;
				}
			}
			const int32 Xcm = T.OriginCm.X + C.X * T.VoxelCm + T.VoxelCm / 2;
			const int32 Ycm = T.OriginCm.Y + C.Y * T.VoxelCm + T.VoxelCm / 2;
			for (const FDCVoxelTower& Tower : Sector.Towers)
			{
				if (InFootprint(Tower, Xcm, Ycm))
				{
					const int32 Blocker = DCVoxelMath::FloorDiv(Tower.TopCm() - T.OriginCm.Z + T.VoxelCm - 1, T.VoxelCm);
					Top = FMath::Max(Top, Blocker);
				}
			}
			Tops.Add(C, Top);
			return Top;
		}
		void Raise(const FIntPoint& C, int32 Top) { Tops.Add(C, Top); }
	};
}

namespace
{
	/** Rubble that settled on a tower's roof drops onto its stump when the tower falls (columns in x, y order). */
	void DropTerrainOver(FDCVoxelSector& Sector, const FDCVoxelTower& T)
	{
		FDCVoxelGrid& G = Sector.Terrain;
		const int32 V = G.VoxelCm;
		const FIntVector Lo = G.ToLocal(T.FootMinCm());
		const FIntVector Hi = G.ToLocal(T.FootMaxCm() - FIntVector(1));
		const int32 Floor = DCVoxelMath::FloorDiv(T.TopCm() - G.OriginCm.Z + V - 1, V);
		const int32 Ceil = DCVoxelMath::FloorDiv(T.Grid.OriginCm.Z + T.ScanMax.Z * T.Grid.VoxelCm - G.OriginCm.Z, V) + 2;
		TArray<uint8> Fallen;
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				Fallen.Reset();
				for (int32 Z = Floor; Z <= Ceil; ++Z)
				{
					const FIntVector P(X, Y, Z);
					if (const uint8 M = G.Data.Get(P))
					{
						Fallen.Add(M);
						G.Data.Set(P, 0);
					}
				}
				if (Fallen.Num() == 0)
				{
					continue;
				}
				int32 Top = 0;
				for (int32 Z = Floor - 1; Z >= 0; --Z)
				{
					if (G.Data.IsSolid(FIntVector(X, Y, Z)))
					{
						Top = Z + 1;
						break;
					}
				}
				for (int32 i = 0; i < Fallen.Num(); ++i)
				{
					G.Data.Set(FIntVector(X, Y, Top + i), Fallen[i]);
				}
			}
		}
	}
}

namespace DCVoxelOps
{
	uint32 Hash(const FIntVector& P, uint32 Salt)
	{
		uint32 H = Salt * 0x9E3779B1u;
		H ^= static_cast<uint32>(P.X) * 0x85EBCA77u;
		H = (H << 13) | (H >> 19);
		H ^= static_cast<uint32>(P.Y) * 0xC2B2AE3Du;
		H = (H << 11) | (H >> 21);
		H ^= static_cast<uint32>(P.Z) * 0x27D4EB2Fu;
		H ^= H >> 15;
		H *= 0x2C1B3C6Du;
		H ^= H >> 12;
		return H;
	}

	int32 CarveSphere(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm)
	{
		if (RadiusCm <= 0)
		{
			return 0;
		}
		const int64 R2 = static_cast<int64>(RadiusCm) * RadiusCm;
		const FIntVector Lo = Grid.ToLocal(CenterCm - FIntVector(RadiusCm));
		const FIntVector Hi = Grid.ToLocal(CenterCm + FIntVector(RadiusCm)) + FIntVector(1);
		return Grid.Data.EditBox(FIntVector(Lo.X, Lo.Y, FMath::Max(Lo.Z, 0)), Hi, true, [&Grid, &CenterCm, R2](const FIntVector& P, uint8 Old) -> uint8
		{
			if (Old == 0)
			{
				return 0;
			}
			const FIntVector D = Grid.CenterCm(P) - CenterCm;
			return static_cast<int64>(D.X) * D.X + static_cast<int64>(D.Y) * D.Y + static_cast<int64>(D.Z) * D.Z <= R2 ? 0 : Old;
		});
	}

	int32 CarveColumn(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 TopZ)
	{
		if (RadiusCm <= 0)
		{
			return 0;
		}
		const int64 R2 = static_cast<int64>(RadiusCm) * RadiusCm;
		const FIntVector Lo = Grid.ToLocal(CenterCm - FIntVector(RadiusCm, RadiusCm, 0));
		const FIntVector Hi = Grid.ToLocal(CenterCm + FIntVector(RadiusCm, RadiusCm, 0));
		return Grid.Data.EditBox(FIntVector(Lo.X, Lo.Y, 0), FIntVector(Hi.X + 1, Hi.Y + 1, TopZ), true, [&Grid, &CenterCm, R2](const FIntVector& P, uint8 Old) -> uint8
		{
			if (Old == 0)
			{
				return 0;
			}
			const FIntVector C = Grid.CenterCm(FIntVector(P.X, P.Y, 0));
			const int64 Dx = C.X - CenterCm.X;
			const int64 Dy = C.Y - CenterCm.Y;
			return Dx * Dx + Dy * Dy <= R2 ? 0 : Old;
		});
	}

	int32 Bite(FDCVoxelTower& T, const FIntVector& FromCm, int32 BiteFloors, int32 BitePercent, int32 BayCm)
	{
		if (BiteFloors <= 0 || BitePercent <= 0)
		{
			return 0;
		}
		FDCVoxelWorld& World = T.Grid.Data;
		const int32 B = FMath::Max(6, BayCm / T.Grid.VoxelCm);
		const FIntVector Dir = DominantDir(TowerCenterCm(T), FromCm); // the face looking at the blast
		const int32 Axis = Dir.X != 0 ? 0 : 1;
		const int32 Other = 1 - Axis;
		const int32 Depth = (T.Max[Axis] - T.Min[Axis]) * FMath::Clamp(BitePercent, 0, 100) / 100;
		const int32 Top = BiteFloors * B;
		const int32 Ragged = FMath::Max(1, B / 2);
		const int32 KStart = T.ScanMin[Axis] - T.Min[Axis];
		// The bitten region in voxels: from the blast-side face inward Depth (minus the ragged hash), full width.
		FIntVector Lo = T.ScanMin, Hi = T.ScanMax;
		Lo.Z = 1;
		Hi.Z = Top + 1;
		return World.EditBox(Lo, Hi, true, [&](const FIntVector& P, uint8 Old) -> uint8
		{
			if (Old == 0)
			{
				return 0;
			}
			const int32 K = Dir[Axis] > 0 ? T.Max[Axis] - 1 - P[Axis] : P[Axis] - T.Min[Axis];
			const int32 D = Depth - static_cast<int32>(Hash(FIntVector(P[Other], P.Z, Axis), 0xB17E) % Ragged);
			return K >= KStart && K < D ? 0 : Old;
		});
	}

	void Structure(FDCVoxelTower& T, const FIntVector& Dir, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out)
	{
		FDCVoxelWorld& World = T.Grid.Data;
		FBox3 Box;
		Box.Read(World, T.ScanMin, T.ScanMax);
		const FIntVector Size = Box.Size;
		const int32 Support = FMath::Clamp(Ctx.Tuning->SupportPercent, 0, 100);

		// 1. Layer support: the highest layer that lost too much of its intact solid breaks; masts and roof details
		//    (layers far smaller than the tower's typical floor) never count as a break.
		int32 Typical = 0;
		for (int32 Count : T.IntactLayers)
		{
			Typical = FMath::Max(Typical, Count);
		}
		int32 Break = INDEX_NONE;
		// A stump is checked again on every op (only below its old break): a later blast can bring it down further.
		const bool bWasFallen = T.bFallen;
		{
			// A stump's bitten band is weak by design: it only breaks again under fresh damage (half the support).
			const int32 Need = bWasFallen ? Support / 2 : Support;
			for (int32 Z = (bWasFallen ? FMath::Min(T.BreakZ - 1, Size.Z - 1) : Size.Z - 1); Z >= 1; --Z)
			{
				const int32 Intact = T.IntactLayers.IsValidIndex(Z) ? T.IntactLayers[Z] : 0;
				if (Intact * 4 < Typical)
				{
					continue;
				}
				int32 Now = 0;
				for (int32 I = Box.Index(0, 0, Z), E = I + Size.X * Size.Y; I < E; ++I)
				{
					Now += Box.V[I] != 0 ? 1 : 0;
				}
				if (static_cast<int64>(Now) * 100 < static_cast<int64>(Intact) * Need)
				{
					Break = Z;
					break;
				}
			}
		}

		// Takes cells from layer ZFrom up that Pick selects; clears them in the grid with one chunk-wise pass over the
		// same layers (only when something was taken).
		auto TakePiece = [&](FDCVoxelPiece& Piece, int32 ZFrom, TFunctionRef<bool(int32, int32, int32)> Pick)
		{
			const int32 Before = Piece.Cells.Num();
			for (int32 Z = FMath::Max(ZFrom, 0); Z < Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 X = 0; X < Size.X; ++X)
					{
						uint8& Cell = Box.V[Box.Index(X, Y, Z)];
						if (Cell != 0 && Pick(X, Y, Z))
						{
							const FIntVector P = Box.Min + FIntVector(X, Y, Z);
							Piece.Cells.Add(P);
							Piece.Materials.Add(Cell);
							Cell = 0;
						}
					}
				}
			}
			if (Piece.Cells.Num() > Before)
			{
				// The box copy is cleared already; clear the grid in one chunk-wise pass (taken cells now read 0 in Box).
				World.EditBox(FIntVector(T.ScanMin.X, T.ScanMin.Y, Box.Min.Z + FMath::Max(ZFrom, 0)), T.ScanMax, true, [&Box](const FIntVector& P, uint8 Old) -> uint8
				{
					const FIntVector L = P - Box.Min;
					return Box.V[Box.Index(L.X, L.Y, L.Z)] == 0 ? 0 : Old;
				});
			}
		};

		if (Break != INDEX_NONE)
		{
			FDCVoxelPiece& Piece = Out.Pieces.AddDefaulted_GetRef();
			Piece.Lot = T.Lot;
			Piece.bTopple = true;
			Piece.Dir = Dir;
			const int32 Axis = Dir.X != 0 ? 0 : 1;
			Piece.Pivot = FIntVector((T.Min.X + T.Max.X) / 2, (T.Min.Y + T.Max.Y) / 2, Break);
			Piece.Pivot[Axis] = Dir[Axis] > 0 ? T.Max[Axis] : T.Min[Axis];
			const int32 LocalBreak = Break; // Box.Min.Z is 0
			TakePiece(Piece, LocalBreak + 1, [](int32, int32, int32) { return true; });
			T.bFallen = true;
			T.BreakZ = Break;
			if (!bWasFallen)
			{
				Out.FallenLots.Add(T.Lot); // the gameplay collapse record is one-shot
			}
			if (Piece.Cells.Num() == 0)
			{
				Out.Pieces.Pop();
			}
		}

		// 2. Islands: anything not connected to the ground drops straight down. Connectivity runs on coarse blocks (K^3
		//    voxels, solid only above a fill count), so a floor never hangs on a single 10 cm voxel (critic: floating
		//    slabs). Cells in blocks that are not reached from the ground fall.
		const int32 K = FMath::Max(1, Ctx.Tuning->IslandBlockVoxels);
		const int32 MinFill = FMath::Max(1, Ctx.Tuning->IslandBlockFill);
		const FIntVector BSize(FMath::DivideAndRoundUp(Size.X, K), FMath::DivideAndRoundUp(Size.Y, K), FMath::DivideAndRoundUp(Size.Z, K));
		auto BIndex = [&BSize](int32 X, int32 Y, int32 Z) { return X + BSize.X * (Y + BSize.Y * Z); };
		TArray<int32> Fill;
		Fill.SetNumZeroed(BSize.X * BSize.Y * BSize.Z);
		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					Fill[BIndex(X / K, Y / K, Z / K)] += Box.V[Box.Index(X, Y, Z)] != 0 ? 1 : 0;
				}
			}
		}
		TBitArray<> Reached(false, Fill.Num());
		TArray<FIntVector> Stack;
		for (int32 Y = 0; Y < BSize.Y; ++Y)
		{
			for (int32 X = 0; X < BSize.X; ++X)
			{
				if (Fill[BIndex(X, Y, 0)] >= MinFill)
				{
					Reached[BIndex(X, Y, 0)] = true;
					Stack.Add(FIntVector(X, Y, 0));
				}
			}
		}
		static const FIntVector Steps[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		while (Stack.Num() > 0)
		{
			const FIntVector P = Stack.Pop(EAllowShrinking::No);
			for (const FIntVector& S : Steps)
			{
				const FIntVector Q = P + S;
				if (Q.X < 0 || Q.Y < 0 || Q.Z < 0 || Q.X >= BSize.X || Q.Y >= BSize.Y || Q.Z >= BSize.Z)
				{
					continue;
				}
				const int32 I = BIndex(Q.X, Q.Y, Q.Z);
				if (Fill[I] >= MinFill && !Reached[I])
				{
					Reached[I] = true;
					Stack.Add(Q);
				}
			}
		}
		FDCVoxelPiece Drop;
		Drop.Lot = T.Lot;
		Drop.Dir = Dir;
		// Only when some block with voxels was not reached is there anything to drop.
		bool bAnyLoose = false;
		for (int32 b = 0; b < Fill.Num() && !bAnyLoose; ++b)
		{
			bAnyLoose = Fill[b] > 0 && !Reached[b];
		}
		if (bAnyLoose)
		{
			TakePiece(Drop, 0, [&Reached, &BIndex, K](int32 X, int32 Y, int32 Z) { return !Reached[BIndex(X / K, Y / K, Z / K)]; });
		}
		if (Drop.Cells.Num() > 0)
		{
			Drop.Pivot = Drop.Cells[0];
			Out.Pieces.Add(MoveTemp(Drop));
		}
	}

	void Settle(FDCVoxelSector& Sector, FDCVoxelPiece& Piece, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out)
	{
		const FDCVoxelTuning& Tn = *Ctx.Tuning;
		FDCVoxelGrid& Terrain = Sector.Terrain;
		const FDCVoxelGrid& Src = Sector.Towers[Piece.Lot].Grid;
		Piece.RubbleStart = Out.Rubble.Num();

		// Downsample to terrain cells, first-seen order (the piece's cells are in scan order: bottom up).
		TArray<FIntVector> Coarse;
		TArray<uint8> CoarseMat;
		TSet<FIntVector> Seen;
		for (int32 i = 0; i < Piece.Cells.Num(); ++i)
		{
			const FIntVector C = Terrain.ToLocal(Src.CenterCm(Piece.Cells[i]));
			bool bAlready = false;
			Seen.Add(C, &bAlready);
			if (!bAlready)
			{
				Coarse.Add(C);
				CoarseMat.Add(Piece.Materials[i]);
			}
		}

		int32 Ceiling = Terrain.ToLocal(FIntVector(0, 0, 0)).Z + 2;
		for (const FDCVoxelTower& T : Sector.Towers)
		{
			Ceiling = FMath::Max(Ceiling, Terrain.ToLocal(FIntVector(0, 0, T.Grid.OriginCm.Z + T.ScanMax.Z * T.Grid.VoxelCm)).Z + 2);
		}
		FColumnTops Tops{Sector, Ceiling};
		const FIntVector Pivot = Terrain.ToLocal(Src.CenterCm(Piece.Pivot));
		const int32 Keep = FMath::Clamp(Tn.RubbleKeepPercent, 0, 100);
		const int32 Axis = Piece.Dir.X != 0 ? 0 : 1;
		const int32 Other = 1 - Axis;
		const int32 Step = FMath::Max(Tn.ReposeStep, 1);
		static const FIntPoint Around[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
		for (int32 i = 0; i < Coarse.Num(); ++i)
		{
			const FIntVector P = Coarse[i];
			const uint32 H = Hash(P, 0x5E771E);
			if (static_cast<int32>(H % 100) >= Keep)
			{
				continue; // turns to dust
			}
			// Drift: a toppling piece is thrown further the higher the voxel sat above the break.
			const int32 Height = FMath::Max(0, P.Z - Pivot.Z);
			int32 Drift = Piece.bTopple ? Height * Tn.ToppleDriftPercent / 100 + static_cast<int32>((H >> 8) % 3) : 0;
			const int32 Side = static_cast<int32>((H >> 12) % 3) - 1;
			FIntPoint Target;
			// Back off toward the original column while the landing column stands taller than the voxel fell from.
			for (;; Drift = Drift * 3 / 4)
			{
				Target = FIntPoint(P.X, P.Y);
				(Axis == 0 ? Target.X : Target.Y) += Piece.Dir[Axis] * Drift;
				(Other == 0 ? Target.X : Target.Y) += Side;
				if (Drift == 0 || Tops.Get(Target) <= P.Z)
				{
					break;
				}
			}
			// Angle of repose: slide to a lower neighbour (search rotation from the hash).
			int32 Top = Tops.Get(Target);
			for (int32 Slide = 0; Slide < MaxSlides; ++Slide)
			{
				bool bMoved = false;
				for (int32 k = 0; k < 4; ++k)
				{
					const FIntPoint N = Target + Around[(k + (H >> 20)) % 4];
					const int32 NTop = Tops.Get(N);
					if (Top - NTop > Step)
					{
						Target = N;
						Top = NTop;
						bMoved = true;
						break;
					}
				}
				if (!bMoved)
				{
					break;
				}
			}
			const FIntVector Cell(Target.X, Target.Y, Top);
			const uint8 Mat = CoarseMat[i];
			const uint8 As = Ctx.RubbleOf.IsValidIndex(Mat) && Ctx.RubbleOf[Mat] != 0 ? Ctx.RubbleOf[Mat] : Mat;
			if (!Terrain.Data.IsSolid(Cell))
			{
				Terrain.Data.Set(Cell, As);
				Out.Rubble.Add(Cell);
				Out.RubbleMaterials.Add(As);
			}
			Tops.Raise(Target, Top + 1);
		}
		Piece.RubbleNum = Out.Rubble.Num() - Piece.RubbleStart;
	}

	FDCVoxelOpResult Apply(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx)
	{
		FDCVoxelOpResult Out;
		double Clock = FPlatformTime::Seconds();
		auto Lap = [&Clock]() { const double Now = FPlatformTime::Seconds(); const float Ms = static_cast<float>((Now - Clock) * 1000.0); Clock = Now; return Ms; };
		const bool bFell = Op.Kind == TEXT("Fell");
		const FDCVoxelCarveDef& Carve = bFell ? Ctx.Tuning->DefaultCarve : Ctx.CarveFor(Op.Stratagem);
		const FIntVector C = Op.CenterCm;
		if (bFell)
		{
			if (Sector.Towers.IsValidIndex(Op.Lot) && !Sector.Towers[Op.Lot].bFallen)
			{
				Out.Carved += Bite(Sector.Towers[Op.Lot], C, Carve.BiteFloors, Carve.BitePercent, Ctx.BayCm);
				Out.TouchedLots.Add(Op.Lot);
			}
		}
		else
		{
			const int32 Reach = FMath::Max(Carve.ColumnRadiusCm, Carve.SphereRadiusCm);
			const int64 Blast2 = static_cast<int64>(Op.BlastRadiusCm) * Op.BlastRadiusCm;
			for (int32 i = 0; i < Sector.Towers.Num(); ++i)
			{
				FDCVoxelTower& T = Sector.Towers[i];
				bool bTouched = false;
				if (DistSqToFootprint(T, C) <= static_cast<int64>(Reach + 100) * (Reach + 100))
				{
					const int32 Carved = CarveColumn(T.Grid, C, Carve.ColumnRadiusCm, T.ScanMax.Z) + CarveSphere(T.Grid, C, Carve.SphereRadiusCm);
					Out.Carved += Carved;
					bTouched = Carved > 0;
				}
				if (!T.bFallen && DistSqToFootprint(T, C) <= Blast2)
				{
					Out.Carved += Bite(T, C, Carve.BiteFloors, Carve.BitePercent, Ctx.BayCm);
					bTouched = true;
				}
				if (bTouched)
				{
					Out.TouchedLots.Add(i);
				}
			}
			Out.Carved += CarveSphere(Sector.Terrain, C, Carve.CraterRadiusCm);
			// Props in the blast are gone, top to bottom (a sphere left their tops floating: review CP2).
			Out.Carved += CarveColumn(Sector.Props, C, Carve.SphereRadiusCm, Sector.Props.ToLocal(FIntVector(0, 0, 4000)).Z);
		}
		Out.CarveMs = Lap();
		// Towers are independent grids: their structure passes run in parallel, and the results merge in lot order
		// (the same order on every machine), so the op stays deterministic.
		TArray<FDCVoxelOpResult> PerTower;
		PerTower.SetNum(Out.TouchedLots.Num());
		ParallelFor(Out.TouchedLots.Num(), [&](int32 k)
		{
			FDCVoxelTower& T = Sector.Towers[Out.TouchedLots[k]];
			Structure(T, DominantDir(C, TowerCenterCm(T)), Ctx, PerTower[k]); // falls away from the blast
		});
		for (FDCVoxelOpResult& R : PerTower)
		{
			Out.FallenLots.Append(R.FallenLots);
			for (FDCVoxelPiece& Piece : R.Pieces)
			{
				Out.Pieces.Add(MoveTemp(Piece));
			}
		}
		for (int32 Lot : Out.FallenLots)
		{
			DropTerrainOver(Sector, Sector.Towers[Lot]); // nothing left floating where the roof was
		}
		Out.StructureMs = Lap();
		for (FDCVoxelPiece& Piece : Out.Pieces)
		{
			Settle(Sector, Piece, Ctx, Out);
		}
		Out.SettleMs = Lap();
		return Out;
	}
}
