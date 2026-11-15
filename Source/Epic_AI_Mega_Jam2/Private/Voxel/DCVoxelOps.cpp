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
			const int32 S = FDCVoxelChunk::Size;
			for (int32 Z = Ceiling; Z >= 0; --Z)
			{
				// Whole empty chunks are skipped (per-voxel lookups through empty sky cost ~1 s per barrage settle).
				const FDCVoxelChunk* Ch = T.Data.FindChunk(FIntVector(DCVoxelMath::FloorDiv(C.X, S), DCVoxelMath::FloorDiv(C.Y, S), DCVoxelMath::FloorDiv(Z, S)));
				if (!Ch || Ch->Solid == 0)
				{
					Z = DCVoxelMath::FloorDiv(Z, S) * S;
					continue;
				}
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
					Top = FMath::Max(Top, DCVoxelOps::TowerTopCells(Tower, T, C.X, C.Y));
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
	/** Drops every terrain cell of column (X, Y) from Floor to Ceil onto the first solid cell below Floor, in order. */
	void DropColumn(FDCVoxelGrid& G, int32 X, int32 Y, int32 Floor, int32 Ceil, TArray<uint8>& Fallen)
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
			return;
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

	/** The terrain cell just above the tallest tower's scan box: nothing settles higher. */
	int32 TerrainCeil(const FDCVoxelSector& Sector)
	{
		const FDCVoxelGrid& G = Sector.Terrain;
		int32 Ceil = 0;
		for (const FDCVoxelTower& T : Sector.Towers)
		{
			Ceil = FMath::Max(Ceil, DCVoxelMath::FloorDiv(T.Grid.OriginCm.Z + T.ScanMax.Z * T.Grid.VoxelCm - G.OriginCm.Z, G.VoxelCm) + 2);
		}
		return Ceil;
	}

	/** Rubble lying over a touched tower drops onto what is left of it, column by column (x, y order): a carved roof
	 *  or a fallen top never leaves a rubble sheet hanging where the voxels were. */
	void DropTerrainOver(FDCVoxelSector& Sector, const FDCVoxelTower& T, int32 GroundLayers)
	{
		FDCVoxelGrid& G = Sector.Terrain;
		const int32 V = G.VoxelCm;
		// The whole scan box, not only the footprint (R23): rubble also rests on what stands proud of the walls (a sign
		// band at the roof line, a wall mount), which TowerTopCells counts as the tower's top; when that falls, the
		// rubble over it drops too.
		const FIntVector Lo = G.ToLocal(T.Grid.MinCm(T.ScanMin));
		const FIntVector Hi = G.ToLocal(T.Grid.MinCm(T.ScanMax) - FIntVector(1));
		const int32 Ceil = DCVoxelMath::FloorDiv(T.Grid.OriginCm.Z + T.ScanMax.Z * T.Grid.VoxelCm - G.OriginCm.Z, V) + 2;
		const int32 Ground = FMath::Max(GroundLayers, 1);
		TArray<uint8> Fallen;
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				DropColumn(G, X, Y, FMath::Max(DCVoxelOps::TowerTopCells(T, G, X, Y), Ground), Ceil, Fallen);
			}
		}
	}

	/** Rubble over a crater drops into it (R23): the ground-level crater sphere removes the low cells of a column near
	 *  its rim and left the rubble above them hanging (Voxel.RubbleNeverFloats). Columns under a tower keep resting on
	 *  what is left of it. */
	void DropTerrainOverCrater(FDCVoxelSector& Sector, const FIntVector& CenterCm, int32 RadiusCm, int32 GroundLayers)
	{
		if (RadiusCm <= 0)
		{
			return;
		}
		FDCVoxelGrid& G = Sector.Terrain;
		const int32 V = G.VoxelCm;
		const FIntVector Lo = G.ToLocal(CenterCm - FIntVector(RadiusCm + V, RadiusCm + V, 0));
		const FIntVector Hi = G.ToLocal(CenterCm + FIntVector(RadiusCm + V, RadiusCm + V, 0));
		const int32 Ceil = TerrainCeil(Sector);
		const int32 Ground = FMath::Max(GroundLayers, 1);
		TArray<uint8> Fallen;
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				int32 Floor = Ground;
				for (const FDCVoxelTower& T : Sector.Towers)
				{
					Floor = FMath::Max(Floor, DCVoxelOps::TowerTopCells(T, G, X, Y));
				}
				DropColumn(G, X, Y, Floor, Ceil, Fallen);
			}
		}
	}
}

namespace DCVoxelOps
{
	bool SightBlocked(const FDCVoxelSector& Sector, const FVector& From, const FVector& To, const TArray<int32>& IgnoreLots, float StepCm, float SkipNearCm, float SkipFarCm)
	{
		const float Len = FVector::Dist(From, To);
		const float Step = FMath::Max(StepCm, 5.f);
		// The last stretch before the target never blocks (the subject's own surface), nor the first (the lens).
		const float Start = SkipNearCm, End = Len - SkipFarCm;
		const FVector Dir = (To - From).GetSafeNormal();
		for (float D = Start; D < End; D += Step)
		{
			const FVector P = From + Dir * D;
			const FIntVector Cm = DCVoxelMath::ToCm(P);
			if (Sector.Terrain.Data.IsSolid(Sector.Terrain.ToLocal(Cm)))
			{
				return true;
			}
			for (int32 i = 0; i < Sector.Towers.Num(); ++i)
			{
				const FDCVoxelTower& T = Sector.Towers[i];
				const FIntVector L = T.Grid.ToLocal(Cm);
				if (L.X < T.ScanMin.X || L.Y < T.ScanMin.Y || L.Z < 0 || L.X >= T.ScanMax.X || L.Y >= T.ScanMax.Y || L.Z >= T.ScanMax.Z || IgnoreLots.Contains(i))
				{
					continue;
				}
				// A standing shell blocks through its hollow inside too (you cannot see through a building).
				if (T.Grid.Data.IsSolid(L) || (!T.bFallen && L.X >= T.Min.X && L.Y >= T.Min.Y && L.X < T.Max.X && L.Y < T.Max.Y && L.Z < T.Max.Z))
				{
					return true;
				}
			}
		}
		return false;
	}

	/** The tower's real top over one terrain column, in terrain cells (0 = no tower voxel there): the highest solid
	 *  tower voxel under the column's square, so rubble rests on what is left, never on the uncarved roof prism. */
	int32 TowerTopCells(const FDCVoxelTower& T, const FDCVoxelGrid& G, int32 X, int32 Y)
	{
		const int32 V = G.VoxelCm;
		const int32 Tv = T.Grid.VoxelCm;
		const int32 X0 = G.OriginCm.X + X * V, Y0 = G.OriginCm.Y + Y * V;
		const int32 Lx0 = FMath::Max(DCVoxelMath::FloorDiv(X0 - T.Grid.OriginCm.X, Tv), T.ScanMin.X);
		const int32 Ly0 = FMath::Max(DCVoxelMath::FloorDiv(Y0 - T.Grid.OriginCm.Y, Tv), T.ScanMin.Y);
		const int32 Lx1 = FMath::Min(DCVoxelMath::FloorDiv(X0 + V - 1 - T.Grid.OriginCm.X, Tv), T.ScanMax.X - 1);
		const int32 Ly1 = FMath::Min(DCVoxelMath::FloorDiv(Y0 + V - 1 - T.Grid.OriginCm.Y, Tv), T.ScanMax.Y - 1);
		if (Lx0 > Lx1 || Ly0 > Ly1)
		{
			return 0;
		}
		const int32 S = FDCVoxelChunk::Size;
		for (int32 Z = T.ScanMax.Z - 1; Z >= 0; --Z)
		{
			// Skip whole empty chunk layers (a fallen stump leaves hundreds of empty layers above it; per-voxel lookups
			// there made every op on a toppled tower cost ~1 s: DCOPTIME structure, review CP26).
			bool bAnyChunk = false;
			for (int32 Cy = DCVoxelMath::FloorDiv(Ly0, S); Cy <= DCVoxelMath::FloorDiv(Ly1, S) && !bAnyChunk; ++Cy)
			{
				for (int32 Cx = DCVoxelMath::FloorDiv(Lx0, S); Cx <= DCVoxelMath::FloorDiv(Lx1, S) && !bAnyChunk; ++Cx)
				{
					const FDCVoxelChunk* C = T.Grid.Data.FindChunk(FIntVector(Cx, Cy, DCVoxelMath::FloorDiv(Z, S)));
					bAnyChunk = C && C->Solid > 0;
				}
			}
			if (!bAnyChunk)
			{
				Z = DCVoxelMath::FloorDiv(Z, S) * S; // the loop's --Z steps into the chunk layer below
				continue;
			}
			for (int32 Ly = Ly0; Ly <= Ly1; ++Ly)
			{
				for (int32 Lx = Lx0; Lx <= Lx1; ++Lx)
				{
					if (T.Grid.Data.IsSolid(FIntVector(Lx, Ly, Z)))
					{
						const int32 TopCm = T.Grid.OriginCm.Z + (Z + 1) * Tv;
						return FMath::Max(0, DCVoxelMath::FloorDiv(TopCm - G.OriginCm.Z + V - 1, V));
					}
				}
			}
		}
		return 0;
	}

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

	int32 CarveSphere(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 MinZ, TArray<TPair<FIntVector, uint8>>* Removed)
	{
		if (RadiusCm <= 0)
		{
			return 0;
		}
		const int64 R2 = static_cast<int64>(RadiusCm) * RadiusCm;
		const FIntVector Lo = Grid.ToLocal(CenterCm - FIntVector(RadiusCm));
		const FIntVector Hi = Grid.ToLocal(CenterCm + FIntVector(RadiusCm)) + FIntVector(1);
		return Grid.Data.EditBox(FIntVector(Lo.X, Lo.Y, FMath::Max(Lo.Z, MinZ)), Hi, true, [&Grid, &CenterCm, R2, Removed](const FIntVector& P, uint8 Old) -> uint8
		{
			if (Old == 0)
			{
				return 0;
			}
			const FIntVector D = Grid.CenterCm(P) - CenterCm;
			const bool bGone = static_cast<int64>(D.X) * D.X + static_cast<int64>(D.Y) * D.Y + static_cast<int64>(D.Z) * D.Z <= R2;
			if (bGone && Removed)
			{
				Removed->Add({P, Old});
			}
			return bGone ? 0 : Old;
		});
	}

	int32 CarveColumn(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 TopZ, TArray<TPair<FIntVector, uint8>>* Removed)
	{
		if (RadiusCm <= 0)
		{
			return 0;
		}
		const int64 R2 = static_cast<int64>(RadiusCm) * RadiusCm;
		const FIntVector Lo = Grid.ToLocal(CenterCm - FIntVector(RadiusCm, RadiusCm, 0));
		const FIntVector Hi = Grid.ToLocal(CenterCm + FIntVector(RadiusCm, RadiusCm, 0));
		return Grid.Data.EditBox(FIntVector(Lo.X, Lo.Y, 0), FIntVector(Hi.X + 1, Hi.Y + 1, TopZ), true, [&Grid, &CenterCm, R2, Removed](const FIntVector& P, uint8 Old) -> uint8
		{
			if (Old == 0)
			{
				return 0;
			}
			const FIntVector C = Grid.CenterCm(FIntVector(P.X, P.Y, 0));
			const int64 Dx = C.X - CenterCm.X;
			const int64 Dy = C.Y - CenterCm.Y;
			const bool bGone = Dx * Dx + Dy * Dy <= R2;
			if (bGone && Removed)
			{
				Removed->Add({P, Old});
			}
			return bGone ? 0 : Old;
		});
	}

	int32 Bite(FDCVoxelTower& T, const FIntVector& FromCm, int32 BiteFloors, int32 BitePercent, int32 BayCm, TArray<TPair<FIntVector, uint8>>* Removed)
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
			const bool bGone = K >= KStart && K < D;
			if (bGone && Removed)
			{
				Removed->Add({P, Old});
			}
			return bGone ? 0 : Old;
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
		// Takes cells inside the local box [Lo, Hi) that Pick selects; clears them in the grid with one chunk-wise pass
		// over that box only (a sign's piece scans the sign, not the tower above it: review CP26, 1.3-2 s ops).
		auto TakePiece = [&](FDCVoxelPiece& Piece, const FIntVector& InLo, const FIntVector& InHi, TFunctionRef<bool(int32, int32, int32)> Pick)
		{
			const FIntVector Lo = InLo.ComponentMax(FIntVector::ZeroValue);
			const FIntVector Hi = InHi.ComponentMin(Size);
			const int32 Before = Piece.Cells.Num();
			for (int32 Z = Lo.Z; Z < Hi.Z; ++Z)
			{
				for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
				{
					for (int32 X = Lo.X; X < Hi.X; ++X)
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
				World.EditBox(Box.Min + Lo, Box.Min + Hi, true, [&Box](const FIntVector& P, uint8 Old) -> uint8
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
			// A sign that crosses the break stays out of the topple piece: the sign pass drops it whole (review CP22).
			TArray<TPair<FIntVector, FIntVector>> Crossing;
			for (const FDCVoxelSign& Sign : T.Signs)
			{
				const FIntVector L0 = Sign.BoxMin - Box.Min;
				const FIntVector L1 = Sign.BoxMax - Box.Min;
				if (L0.Z <= LocalBreak && L1.Z > LocalBreak + 1)
				{
					Crossing.Emplace(L0, L1);
				}
			}
			TakePiece(Piece, FIntVector(0, 0, LocalBreak + 1), Size, [&Crossing](int32 X, int32 Y, int32 Z)
			{
				return !Crossing.ContainsByPredicate([X, Y, Z](const TPair<FIntVector, FIntVector>& B)
				{
					return X >= B.Key.X && Y >= B.Key.Y && Z >= B.Key.Z && X < B.Value.X && Y < B.Value.Y && Z < B.Value.Z;
				});
			});
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

		// 2. Signs: a sign whose wall is mostly gone falls whole, as its own piece, before the island pass can split it
		//    (review CP21; it would hang off one corner otherwise).
		const int32 Hold = FMath::Clamp(Ctx.Tuning->SignHoldPercent, 0, 100);
		for (const FDCVoxelSign& Sign : T.Signs)
		{
			// Box.Min.Z is 0 (the scan box starts at the ground), so local and grid z agree here.
			const bool bCrossesBreak = Break != INDEX_NONE && Sign.BoxMin.Z - Box.Min.Z <= Break && Sign.BoxMax.Z - Box.Min.Z > Break + 1;
			if (Sign.BackCells <= 0 && !bCrossesBreak)
			{
				continue;
			}
			const FIntVector B0 = Sign.BackMin - Box.Min;
			const FIntVector B1 = Sign.BackMax - Box.Min;
			int32 Left = 0;
			for (int32 Z = FMath::Max(B0.Z, 0); Z < FMath::Min(B1.Z, Size.Z); ++Z)
			{
				for (int32 Y = FMath::Max(B0.Y, 0); Y < FMath::Min(B1.Y, Size.Y); ++Y)
				{
					for (int32 X = FMath::Max(B0.X, 0); X < FMath::Min(B1.X, Size.X); ++X)
					{
						Left += Box.V[Box.Index(X, Y, Z)] != 0 ? 1 : 0;
					}
				}
			}
			if (!bCrossesBreak && static_cast<int64>(Left) * 100 >= static_cast<int64>(FMath::Max(Sign.BackCells, 1)) * Hold)
			{
				continue;
			}
			const FIntVector S0 = Sign.BoxMin - Box.Min;
			const FIntVector S1 = Sign.BoxMax - Box.Min;
			FDCVoxelPiece SignPiece;
			SignPiece.Lot = T.Lot;
			SignPiece.Dir = Dir;
			TakePiece(SignPiece, S0, S1, [S0, S1](int32 X, int32 Y, int32 Z)
			{
				return X >= S0.X && Y >= S0.Y && Z >= S0.Z && X < S1.X && Y < S1.Y && Z < S1.Z;
			});
			if (SignPiece.Cells.Num() > 0)
			{
				SignPiece.Pivot = SignPiece.Cells[0];
				Out.Pieces.Add(MoveTemp(SignPiece));
			}
		}

		// 3. Islands: anything not connected to the ground drops straight down. Connectivity runs on coarse blocks (K^3
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
			TakePiece(Drop, FIntVector::ZeroValue, Size, [&Reached, &BIndex, K](int32 X, int32 Y, int32 Z) { return !Reached[BIndex(X / K, Y / K, Z / K)]; });
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

	/** The shared tail of every op that can bring a tower down (advisor a-37): structure pass on Lots (in parallel,
	 *  merged in lot order), terrain over them dropped, then the pieces settle. Strike and Fell run it on every lot they
	 *  touched; Chip only on the lots the host listed in StructureLots. */
	template <typename FLap>
	void StructureTail(FDCVoxelSector& Sector, const TArray<int32>& Lots, const FIntVector& C, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out, FLap& Lap, int32 CraterRadiusCm = 0)
	{
		// Towers are independent grids: their structure passes run in parallel, and the results merge in lot order
		// (the same order on every machine), so the op stays deterministic.
		TArray<FDCVoxelOpResult> PerTower;
		PerTower.SetNum(Lots.Num());
		ParallelFor(Lots.Num(), [&](int32 k)
		{
			FDCVoxelTower& T = Sector.Towers[Lots[k]];
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
		for (int32 Lot : Lots)
		{
			DropTerrainOver(Sector, Sector.Towers[Lot], Ctx.Tuning->GroundLayers); // nothing left floating where a roof or a top was (lot order)
		}
		DropTerrainOverCrater(Sector, C, CraterRadiusCm, Ctx.Tuning->GroundLayers); // strikes: rubble over the crater rim drops in
		Out.StructureMs = Lap();
		for (FDCVoxelPiece& Piece : Out.Pieces)
		{
			Settle(Sector, Piece, Ctx, Out);
		}
		Out.SettleMs = Lap();
	}

	/** Op clock: each call returns the ms since the previous one (carve, structure, settle evidence). */
	struct FOpLap
	{
		double Clock = FPlatformTime::Seconds();
		float operator()()
		{
			const double Now = FPlatformTime::Seconds();
			const float Ms = static_cast<float>((Now - Clock) * 1000.0);
			Clock = Now;
			return Ms;
		}
	};

	FDCVoxelOpResult ApplyFell(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece&)
	{
		FDCVoxelOpResult Out;
		FOpLap Lap;
		const FDCVoxelCarveDef& Carve = Ctx.Tuning->DefaultCarve;
		const FIntVector C = Op.CenterCm;
		if (Sector.Towers.IsValidIndex(Op.Lot) && !Sector.Towers[Op.Lot].bFallen)
		{
			TArray<TPair<FIntVector, uint8>> Removed;
			Out.Carved += Bite(Sector.Towers[Op.Lot], C, Carve.BiteFloors, Carve.BitePercent, Ctx.BayCm, &Removed);
			for (const TPair<FIntVector, uint8>& R : Removed)
			{
				Out.Shards.Add({Op.Lot, R.Key, R.Value});
			}
			Out.TouchedLots.Add(Op.Lot);
		}
		Out.CarveMs = Lap();
		StructureTail(Sector, Out.TouchedLots, C, Ctx, Out, Lap);
		return Out;
	}

	FDCVoxelOpResult ApplyStrike(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece&)
	{
		FDCVoxelOpResult Out;
		FOpLap Lap;
		const FDCVoxelCarveDef& Carve = Ctx.CarveFor(Op.Stratagem);
		const FIntVector C = Op.CenterCm;
		const int32 Reach = FMath::Max(Carve.ColumnRadiusCm, Carve.SphereRadiusCm);
		const int64 Blast2 = static_cast<int64>(Op.BlastRadiusCm) * Op.BlastRadiusCm;
		for (int32 i = 0; i < Sector.Towers.Num(); ++i)
		{
			FDCVoxelTower& T = Sector.Towers[i];
			bool bTouched = false;
			TArray<TPair<FIntVector, uint8>> Removed;
			if (DistSqToFootprint(T, C) <= static_cast<int64>(Reach + 100) * (Reach + 100))
			{
				const int32 Carved = CarveColumn(T.Grid, C, Carve.ColumnRadiusCm, T.ScanMax.Z, &Removed) + CarveSphere(T.Grid, C, Carve.SphereRadiusCm, 0, &Removed);
				Out.Carved += Carved;
				bTouched = Carved > 0;
			}
			if (!T.bFallen && DistSqToFootprint(T, C) <= Blast2)
			{
				Out.Carved += Bite(T, C, Carve.BiteFloors, Carve.BitePercent, Ctx.BayCm, &Removed);
				bTouched = true;
			}
			for (const TPair<FIntVector, uint8>& R : Removed)
			{
				Out.Shards.Add({i, R.Key, R.Value});
			}
			if (bTouched)
			{
				Out.TouchedLots.Add(i);
			}
		}
		// The crater never punches through the bedrock layers (a hole to the void read as a checkerboard).
		Out.Carved += CarveSphere(Sector.Terrain, C, Carve.CraterRadiusCm, FMath::Max(Ctx.Tuning->BedrockLayers, 0));
		// Props in the blast are gone, top to bottom (a sphere left their tops floating: review CP2).
		Out.Carved += CarveColumn(Sector.Props, C, Carve.SphereRadiusCm, Sector.Props.ToLocal(FIntVector(0, 0, 4000)).Z);
		Out.CarveMs = Lap();
		StructureTail(Sector, Out.TouchedLots, C, Ctx, Out, Lap, Carve.CraterRadiusCm);
		return Out;
	}

	/** R24 weapon chips (advisor a-37): clear the listed spheres (tower grid of a lot, or the prop grid), then the
	 *  structure pass on StructureLots only. Every chip leaves one crumb cluster (cosmetic, from the voxels it
	 *  removed), never physics shard bodies. */
	FDCVoxelOpResult ApplyChip(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece&)
	{
		FDCVoxelOpResult Out;
		FOpLap Lap;
		TArray<TPair<FIntVector, uint8>> Removed;
		for (const FDCVoxelChip& Chip : Op.Chips)
		{
			const bool bTower = Sector.Towers.IsValidIndex(Chip.Grid);
			if (!bTower && Chip.Grid != PropGrid)
			{
				Out.ChipVoxels.Add(0);
				continue; // an unknown grid is skipped on every machine alike
			}
			FDCVoxelGrid& Grid = bTower ? Sector.Towers[Chip.Grid].Grid : Sector.Props;
			Removed.Reset();
			const int32 Carved = CarveSphere(Grid, Chip.CenterCm, FMath::Clamp(Chip.RadiusCm, 1, ChipMaxRadiusCm), 0, &Removed);
			Out.ChipVoxels.Add(Carved);
			if (Carved == 0)
			{
				continue;
			}
			Out.Carved += Carved;
			if (bTower)
			{
				Out.TouchedLots.AddUnique(Chip.Grid);
				const int32 SupportTop = Ctx.Tuning->ChipSupportFloors * FMath::Max(6, Ctx.BayCm / FMath::Max(Grid.VoxelCm, 1));
				if (SupportTop <= 0 || Grid.ToLocal(Chip.CenterCm).Z < SupportTop)
				{
					Out.ChipCarved.Add(FIntPoint(Chip.Grid, Carved)); // support the host counts toward a structure pass
				}
			}
			FDCShardCluster& Cl = Out.ShardClusters.AddDefaulted_GetRef();
			Cl.Pos = FVector(Chip.CenterCm);
			Cl.Count = Carved;
			Cl.Material = Removed[0].Value;
			Cl.VoxelCm = Grid.VoxelCm;
		}
		// Broken props: their column clears top to bottom; the voxels fly as a few pieces and grit (cosmetic).
		for (const FDCVoxelChip& Prop : Op.BreakProps)
		{
			Removed.Reset();
			const int32 Top = Sector.Props.ToLocal(FIntVector(Prop.CenterCm.X, Prop.CenterCm.Y, FMath::Clamp(Prop.CenterCm.Z, 0, 4000))).Z + 1; // CenterCm.Z: the prop's top
			const int32 Carved = CarveColumn(Sector.Props, Prop.CenterCm, FMath::Clamp(Prop.RadiusCm, 1, ChipMaxRadiusCm), Top, &Removed);
			Out.Carved += Carved;
			constexpr int32 PiecesPerProp = 6;
			for (int32 k = 0; k < PiecesPerProp && Removed.Num() > 0; ++k)
			{
				// Slice k of the removed voxels (they come out in grid order: bottom to top), as one piece at its mean.
				const int32 From = Removed.Num() * k / PiecesPerProp, To = Removed.Num() * (k + 1) / PiecesPerProp;
				if (To <= From)
				{
					continue;
				}
				FVector Sum = FVector::ZeroVector;
				for (int32 i = From; i < To; ++i)
				{
					Sum += FVector(Sector.Props.CenterCm(Removed[i].Key));
				}
				FDCShardCluster& Cl = Out.ShardClusters.AddDefaulted_GetRef();
				Cl.Pos = Sum / (To - From);
				Cl.Count = To - From;
				Cl.Material = Removed[From].Value;
				Cl.VoxelCm = Sector.Props.VoxelCm;
				Cl.bPiece = true;
			}
		}
		Out.CarveMs = Lap();
		TArray<int32> Lots;
		for (int32 Lot : Op.StructureLots)
		{
			if (Sector.Towers.IsValidIndex(Lot))
			{
				Lots.AddUnique(Lot);
				Out.TouchedLots.AddUnique(Lot);
			}
		}
		Lots.Sort(); // lot order on every machine
		for (int32 Lot : Lots)
		{
			// The shot-away base gives way on the shooters' side (ChipBite*): the structure pass then topples it away.
			if (!Sector.Towers[Lot].bFallen)
			{
				Out.Carved += Bite(Sector.Towers[Lot], Op.CenterCm, Ctx.Tuning->ChipBiteFloors, Ctx.Tuning->ChipBitePercent, Ctx.BayCm);
			}
		}
		StructureTail(Sector, Lots, Op.CenterCm, Ctx, Out, Lap);
		return Out;
	}

	FDCVoxelOpResult ApplyRestKind(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece& Pend)
	{
		return ApplyRest(Sector, Op, Pend, Ctx);
	}

	/** The op kind registry (rule 3): one row per kind, found by the op's Kind. A new kind is a new row. */
	const TMap<FName, FDCOpKind>& Kinds()
	{
		static const TMap<FName, FDCOpKind> Table = []()
		{
			TMap<FName, FDCOpKind> T;
			T.Add(TEXT("Strike"), FDCOpKind{&ApplyStrike, true, true});
			T.Add(TEXT("Fell"), FDCOpKind{&ApplyFell, true, true});
			T.Add(TEXT("Rest"), FDCOpKind{&ApplyRestKind, true, true});
			T.Add(TEXT("Chip"), FDCOpKind{&ApplyChip, false, false});
			return T;
		}();
		return Table;
	}

	const FDCOpKind* FindKind(FName Kind)
	{
		return Kinds().Find(Kind);
	}

	bool WritesTerrain(const FDCVoxelOp& Op)
	{
		const FDCOpKind* K = FindKind(Op.Kind);
		return !K || K->bWritesTerrain || Op.StructureLots.Num() > 0;
	}

	bool IsChipOnly(const FDCVoxelOp& Op)
	{
		const FDCOpKind* K = FindKind(Op.Kind);
		return K && !K->bWritesTerrain && Op.StructureLots.Num() == 0;
	}

	FDCVoxelOpResult Run(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece& Pend)
	{
		const FDCOpKind* K = FindKind(Op.Kind);
		if (!K)
		{
			UE_LOG(LogTemp, Warning, TEXT("DCVOXEL op seq=%d: unknown kind %s skipped"), Op.Seq, *Op.Kind.ToString());
			return FDCVoxelOpResult();
		}
		return K->Apply(Sector, Op, Ctx, Pend);
	}

	FDCVoxelOpResult Apply(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx)
	{
		return Run(Sector, Op, Ctx, FDCPendingPiece());
	}

	TArray<TArray<int32>> SplitFragments(const TArray<FIntVector>& Cells, int32 BlockCells, int32 MaxFragments, const FIntVector* Focus, float FalloffCells)
	{
		// Budgeted split (advisor CP23): start with one fragment and keep halving the one with the longest extent along
		// that axis until the budget is spent or every fragment is at most BlockCells long. Doubling a block size to
		// fit a cap turned hollow tower tops into 16 m facade sheets. Coordinates decide everything (ties by the
		// fragment's min corner), so the tree never depends on the order of the piece's cells.
		TArray<TArray<int32>> Out;
		if (Cells.Num() == 0)
		{
			return Out;
		}
		struct FGroup
		{
			TArray<int32> Idx;
			FIntVector Lo, Hi; // inclusive bounds
		};
		auto Bounds = [&Cells](FGroup& G)
		{
			G.Lo = FIntVector(MAX_int32);
			G.Hi = FIntVector(MIN_int32);
			for (int32 i : G.Idx)
			{
				G.Lo = G.Lo.ComponentMin(Cells[i]);
				G.Hi = G.Hi.ComponentMax(Cells[i]);
			}
		};
		auto Longest = [](const FGroup& G, int32& Axis)
		{
			const FIntVector E = G.Hi - G.Lo + FIntVector(1);
			Axis = E.X >= E.Y && E.X >= E.Z ? 0 : (E.Y >= E.Z ? 1 : 2);
			return E[Axis];
		};
		auto Before = [](const FIntVector& A, const FIntVector& B)
		{
			return A.Z != B.Z ? A.Z < B.Z : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
		};
		TArray<FGroup> Groups;
		FGroup& First = Groups.AddDefaulted_GetRef();
		First.Idx.Reserve(Cells.Num());
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			First.Idx.Add(i);
		}
		Bounds(First);
		const int32 Cap = FMath::Max(MaxFragments, 1);
		const int32 MinLen = FMath::Max(BlockCells, 1);
		while (Groups.Num() < Cap)
		{
			// Largest extent first, nearer the focus (the blast) weighted up when one is given (advisor CP23).
			int32 Pick = INDEX_NONE, PickLen = 0, PickAxis = 0;
			double PickScore = -1.0;
			for (int32 g = 0; g < Groups.Num(); ++g)
			{
				int32 Axis = 0;
				const int32 Len = Longest(Groups[g], Axis);
				if (Len <= MinLen)
				{
					continue;
				}
				double Score = Len;
				if (Focus && FalloffCells > 0.f)
				{
					const FVector Center = (FVector(Groups[g].Lo) + FVector(Groups[g].Hi)) * 0.5;
					Score = Len / (1.0 + FVector::Dist(Center, FVector(*Focus)) / FalloffCells);
				}
				if (Score > PickScore || (Score == PickScore && Before(Groups[g].Lo, Groups[Pick].Lo)))
				{
					Pick = g;
					PickLen = Len;
					PickAxis = Axis;
					PickScore = Score;
				}
			}
			if (Pick == INDEX_NONE)
			{
				break;
			}
			const int32 Mid = Groups[Pick].Lo[PickAxis] + PickLen / 2;
			FGroup A, B;
			for (int32 i : Groups[Pick].Idx)
			{
				(Cells[i][PickAxis] < Mid ? A : B).Idx.Add(i);
			}
			if (A.Idx.Num() == 0 || B.Idx.Num() == 0)
			{
				break; // cannot happen with Len > 1, kept as a guard against a stuck loop
			}
			Bounds(A);
			Bounds(B);
			Groups[Pick] = MoveTemp(A);
			Groups.Add(MoveTemp(B));
		}
		Groups.Sort([&Before](const FGroup& A, const FGroup& B) { return Before(A.Lo, B.Lo); });
		for (FGroup& G : Groups)
		{
			G.Idx.Sort(); // cell order inside a fragment follows the piece (identical on every machine)
			Out.Add(MoveTemp(G.Idx));
		}
		return Out;
	}

	void FragmentTree(const TArray<FIntVector>& Cells, int32 VoxelCm, float ChunkCm, int32 MaxChunks, float LeafCm, int32 MaxLeaves,
		TArray<int32>& OutParent, TArray<TArray<int32>>& OutCells, const FIntVector* Focus, float FocusFalloffCm)
	{
		OutParent.Reset();
		OutCells.Reset();
		const int32 Vox = FMath::Max(VoxelCm, 1);
		const float Falloff = FocusFalloffCm / Vox;
		for (const TArray<int32>& Group : SplitFragments(Cells, FMath::Max(1, FMath::RoundToInt(ChunkCm / Vox)), MaxChunks, Focus, Falloff))
		{
			const int32 Chunk = OutParent.Num();
			OutParent.Add(INDEX_NONE);
			OutCells.Add(Group);
			TArray<FIntVector> GroupCells;
			for (int32 i : Group)
			{
				GroupCells.Add(Cells[i]);
			}
			for (const TArray<int32>& Leaf : SplitFragments(GroupCells, FMath::Max(1, FMath::RoundToInt(LeafCm / Vox)), MaxLeaves, Focus, Falloff))
			{
				TArray<int32>& Mapped = OutCells.AddDefaulted_GetRef();
				OutParent.Add(Chunk);
				for (int32 k : Leaf)
				{
					Mapped.Add(Group[k]);
				}
			}
		}
	}

	uint8 AxisIndex(const FVector& Dir)
	{
		const FVector A = Dir.GetAbs();
		if (A.X >= A.Y && A.X >= A.Z)
		{
			return Dir.X >= 0.f ? 0 : 1;
		}
		if (A.Y >= A.Z)
		{
			return Dir.Y >= 0.f ? 2 : 3;
		}
		return Dir.Z >= 0.f ? 4 : 5;
	}

	FIntVector AxisDir(uint8 Index)
	{
		static const FIntVector Dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		return Dirs[Index % 6];
	}

	FDCVoxelOpResult ApplyRest(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCPendingPiece& P, const FDCVoxelOpContext& Ctx)
	{
		FDCVoxelOpResult Out;
		if (!Sector.Towers.IsValidIndex(P.Lot))
		{
			return Out;
		}
		// 1. The provisional rubble this piece left at plan time goes (only where it still lies).
		for (int32 i = 0; i < P.Rubble.Num(); ++i)
		{
			if (Sector.Terrain.Data.Get(P.Rubble[i]) == P.RubbleMaterials[i])
			{
				Sector.Terrain.Data.Set(P.Rubble[i], 0);
			}
		}
		// 2. Each rested fragment: its cells turned to the snapped frame about the fragment's centre, moved to the
		//    host's rest centre, then settled like any falling piece (column stacking: nothing floats).
		const FDCVoxelGrid& G = Sector.Towers[P.Lot].Grid;
		for (const FDCVoxelRest& R : Op.Rests)
		{
			if (!P.FragCells.IsValidIndex(R.Frag))
			{
				continue;
			}
			const FIntVector X = AxisDir(R.AxisX);
			FIntVector Z = AxisDir(R.AxisZ);
			if (FMath::Abs(X.X * Z.X + X.Y * Z.Y + X.Z * Z.Z) != 0)
			{
				Z = AxisDir(X.Z != 0 ? 0 : 4); // a broken frame from rounding: any perpendicular axis
			}
			const FIntVector Y(Z.Y * X.Z - Z.Z * X.Y, Z.Z * X.X - Z.X * X.Z, Z.X * X.Y - Z.Y * X.X); // Z x X
			FIntVector Lo(MAX_int32), Hi(MIN_int32);
			for (int32 i : P.FragCells[R.Frag])
			{
				const FIntVector C = G.CenterCm(P.Cells[i]);
				Lo = FIntVector(FMath::Min(Lo.X, C.X), FMath::Min(Lo.Y, C.Y), FMath::Min(Lo.Z, C.Z));
				Hi = FIntVector(FMath::Max(Hi.X, C.X), FMath::Max(Hi.Y, C.Y), FMath::Max(Hi.Z, C.Z));
			}
			const FIntVector Mid = (Lo + Hi) / 2;
			FDCVoxelPiece Piece;
			Piece.Lot = P.Lot;
			Piece.bTopple = false;
			for (int32 i : P.FragCells[R.Frag])
			{
				const FIntVector D = G.CenterCm(P.Cells[i]) - Mid;
				const FIntVector W = R.PosCm + X * D.X + Y * D.Y + Z * D.Z;
				Piece.Cells.Add(G.ToLocal(W));
				Piece.Materials.Add(P.Materials[i]);
			}
			Piece.Pivot = Piece.Cells.Num() > 0 ? Piece.Cells[0] : FIntVector::ZeroValue;
			Settle(Sector, Piece, Ctx, Out);
		}
		return Out;
	}

	void DeriveCosmetics(const FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Result)
	{
		const FDCVoxelTuning& Tn = *Ctx.Tuning;
		const FDCVoxelCarveDef& Detail = Ctx.CarveFor(Op.Stratagem);
		const int32 Pieces = Detail.PhysicsPieces >= 0 ? Detail.PhysicsPieces : Tn.PhysicsPieces;
		const int32 Fragments = Detail.PhysicsFragments >= 0 ? Detail.PhysicsFragments : Tn.PhysicsFragments;
		// Physics pieces: the biggest PhysicsPieces (index order breaks ties), each with its fragment tree.
		TArray<int32> BySize;
		for (int32 i = 0; i < Result.Pieces.Num(); ++i)
		{
			BySize.Add(i);
		}
		BySize.StableSort([&Result](int32 A, int32 B) { return Result.Pieces[A].Cells.Num() > Result.Pieces[B].Cells.Num(); });
		for (int32 k = 0; k < BySize.Num() && k < Pieces; ++k)
		{
			const FDCVoxelPiece& Pc = Result.Pieces[BySize[k]];
			FDCPendingPiece P;
			P.Lot = Pc.Lot;
			P.Cells = Pc.Cells;
			P.Materials = Pc.Materials;
			for (int32 r = Pc.RubbleStart; r < Pc.RubbleStart + Pc.RubbleNum && Result.Rubble.IsValidIndex(r); ++r)
			{
				P.Rubble.Add(Result.Rubble[r]);
				P.RubbleMaterials.Add(Result.RubbleMaterials.IsValidIndex(r) ? Result.RubbleMaterials[r] : 0);
			}
			const FIntVector Focus = Sector.Towers[Pc.Lot].Grid.ToLocal(Op.CenterCm); // smaller fragments near the blast
			FragmentTree(Pc.Cells, Sector.Towers[Pc.Lot].Grid.VoxelCm, Tn.PhysicsFragmentCm, Fragments, Tn.PhysicsLeafCm, Tn.PhysicsLeavesPerChunk, P.FragParent, P.FragCells,
				&Focus, Tn.PhysicsFocusFalloffCm);
			P.FragLo.Init(FIntVector(MAX_int32), P.FragCells.Num());
			P.FragHi.Init(FIntVector(MIN_int32), P.FragCells.Num());
			for (int32 f = 0; f < P.FragCells.Num(); ++f)
			{
				for (int32 i : P.FragCells[f])
				{
					const FIntVector& C = Pc.Cells[i];
					P.FragLo[f] = FIntVector(FMath::Min(P.FragLo[f].X, C.X), FMath::Min(P.FragLo[f].Y, C.Y), FMath::Min(P.FragLo[f].Z, C.Z));
					P.FragHi[f] = FIntVector(FMath::Max(P.FragHi[f].X, C.X), FMath::Max(P.FragHi[f].Y, C.Y), FMath::Max(P.FragHi[f].Z, C.Z));
				}
			}
			Result.Pending.Add({BySize[k], MoveTemp(P)});
		}
		// Shards: clusters grow with the distance from the blast centre; a seeded sample under the cap, weighted toward
		// the centre (the column carve reaches the whole height).
		if (Tn.ShardMax > 0 && Result.Shards.Num() > 0)
		{
			struct FAcc
			{
				FVector Sum = FVector::ZeroVector;
				int32 Count = 0;
				uint8 Mat = 0;
				int32 Block = 1;
				float Vcm = 10.f;
				uint32 Key = 0;
				float Rank = 0.f;
			};
			const FVector Center(Op.CenterCm);
			TMap<FIntVector4, int32> Index;
			TArray<FAcc> Acc;
			for (const FDCVoxelShard& S : Result.Shards)
			{
				if (!Sector.Towers.IsValidIndex(S.Lot))
				{
					continue;
				}
				const FDCVoxelGrid& G = Sector.Towers[S.Lot].Grid;
				const FVector P = G.ToWorld(S.Cell) + FVector(G.VoxelCm * 0.5f);
				const int32 Block = FMath::Clamp(1 + FMath::FloorToInt(FVector::Dist(P, Center) / FMath::Max(Tn.ShardGrowCm, 1.f)), 1, FMath::Max(Tn.ShardBlockMax, 1));
				const FIntVector4 K(S.Lot * 8 + Block, FMath::FloorToInt(static_cast<float>(S.Cell.X) / Block), FMath::FloorToInt(static_cast<float>(S.Cell.Y) / Block), FMath::FloorToInt(static_cast<float>(S.Cell.Z) / Block));
				int32* Found = Index.Find(K);
				if (!Found)
				{
					Found = &Index.Add(K, Acc.Num());
					FAcc& A = Acc.AddDefaulted_GetRef();
					A.Mat = S.Material;
					A.Block = Block;
					A.Vcm = G.VoxelCm;
					A.Key = Hash(FIntVector(K.Y, K.Z, K.W), static_cast<uint32>(K.X));
				}
				FAcc& A = Acc[*Found];
				A.Sum += P;
				++A.Count;
			}
			for (FAcc& A : Acc)
			{
				A.Rank = FVector::Dist(A.Sum / A.Count, Center) * (0.35f + static_cast<float>(A.Key % 1000u) / 1000.f);
			}
			Acc.Sort([](const FAcc& A, const FAcc& B) { return A.Rank != B.Rank ? A.Rank < B.Rank : A.Key < B.Key; });
			for (int32 i = 0; i < Acc.Num() && i < Tn.ShardMax; ++i)
			{
				FDCShardCluster& C = Result.ShardClusters.AddDefaulted_GetRef();
				C.Pos = Acc[i].Sum / Acc[i].Count;
				C.Count = Acc[i].Count;
				C.Material = Acc[i].Mat;
				C.Block = Acc[i].Block;
				C.VoxelCm = Acc[i].Vcm;
			}
		}
		Result.Shards.Empty(); // the raw list is large; only the clusters are kept
	}
}
