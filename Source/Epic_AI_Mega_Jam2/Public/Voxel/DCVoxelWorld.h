// Chunked voxel storage (R1). Pure data: no rendering, no UObject; automation tests run it under -nullrhi.
// A voxel is one byte: 0 = air, otherwise a palette material id. Chunks are 32^3 and allocated on first write.
#pragma once

#include "CoreMinimal.h"

struct EPIC_AI_MEGA_JAM2_API FDCVoxelChunk
{
	static constexpr int32 Bits = 5;
	static constexpr int32 Size = 1 << Bits;
	static constexpr int32 Mask = Size - 1;
	static constexpr int32 Volume = Size * Size * Size;

	uint8 V[Volume];
	int32 Solid = 0;

	FDCVoxelChunk() { FMemory::Memzero(V, sizeof(V)); }
	static int32 Index(int32 X, int32 Y, int32 Z) { return X | (Y << Bits) | (Z << (2 * Bits)); }
};

class EPIC_AI_MEGA_JAM2_API FDCVoxelWorld
{
public:
	FDCVoxelWorld() = default;
	FDCVoxelWorld(FDCVoxelWorld&&) = default;
	FDCVoxelWorld& operator=(FDCVoxelWorld&&) = default;
	FDCVoxelWorld(const FDCVoxelWorld&) = delete;
	FDCVoxelWorld& operator=(const FDCVoxelWorld&) = delete;

	static FIntVector ChunkOf(const FIntVector& P)
	{
		return FIntVector(P.X >> FDCVoxelChunk::Bits, P.Y >> FDCVoxelChunk::Bits, P.Z >> FDCVoxelChunk::Bits);
	}
	static FIntVector LocalOf(const FIntVector& P)
	{
		return FIntVector(P.X & FDCVoxelChunk::Mask, P.Y & FDCVoxelChunk::Mask, P.Z & FDCVoxelChunk::Mask);
	}

	uint8 Get(const FIntVector& P) const
	{
		const TUniquePtr<FDCVoxelChunk>* C = Chunks.Find(ChunkOf(P));
		if (!C)
		{
			return 0;
		}
		const FIntVector L = LocalOf(P);
		return (*C)->V[FDCVoxelChunk::Index(L.X, L.Y, L.Z)];
	}
	bool IsSolid(const FIntVector& P) const { return Get(P) != 0; }

	/** Writes one voxel and marks its chunk (and a neighbour chunk when on a border) dirty for remeshing. */
	void Set(const FIntVector& P, uint8 Material);
	/** Bulk edit, chunk by chunk (one map lookup per chunk, not per voxel): Fn(Position, OldValue) returns the new
	 *  value for every voxel in [Min, Max). Only existing chunks are visited when bSkipEmpty (carving never creates).
	 *  Returns how many voxels changed. Solid counts and dirty chunks stay correct. */
	template <typename FnType>
	int32 EditBox(const FIntVector& Min, const FIntVector& Max, bool bSkipEmpty, FnType&& Fn);

	/** Fills an axis-aligned box, Min inclusive, Max exclusive. */
	void FillBox(const FIntVector& Min, const FIntVector& Max, uint8 Material);

	void Reset() { Chunks.Reset(); Dirty.Reset(); }
	int32 NumChunks() const { return Chunks.Num(); }
	int64 CountSolid() const;
	/** Order-independent content hash: two machines with the same voxels get the same value (determinism checks). */
	uint32 ContentHash() const;

	/** Copies the chunk plus a one-voxel border into Out (34^3, x fastest) for a mesher job. */
	void CopyPadded(const FIntVector& ChunkKey, TArray<uint8>& Out) const;

	const FDCVoxelChunk* FindChunk(const FIntVector& Key) const
	{
		const TUniquePtr<FDCVoxelChunk>* C = Chunks.Find(Key);
		return C ? C->Get() : nullptr;
	}
	template <typename Fn> void ForEachChunk(Fn&& F) const
	{
		for (const TPair<FIntVector, TUniquePtr<FDCVoxelChunk>>& Pair : Chunks)
		{
			F(Pair.Key, *Pair.Value);
		}
	}

	/** Chunks written since the last TakeDirty. */
	TSet<FIntVector> TakeDirty() { TSet<FIntVector> Out = MoveTemp(Dirty); Dirty.Reset(); return Out; }
	bool HasDirty() const { return Dirty.Num() > 0; }

private:
	void MarkDirtyWithNeighbours(const FIntVector& Key);

	TMap<FIntVector, TUniquePtr<FDCVoxelChunk>> Chunks;
	TSet<FIntVector> Dirty;
};

template <typename FnType>
int32 FDCVoxelWorld::EditBox(const FIntVector& Min, const FIntVector& Max, bool bSkipEmpty, FnType&& Fn)
{
	if (Min.X >= Max.X || Min.Y >= Max.Y || Min.Z >= Max.Z)
	{
		return 0;
	}
	constexpr int32 S = FDCVoxelChunk::Size;
	const FIntVector C0 = ChunkOf(Min);
	const FIntVector C1 = ChunkOf(Max - FIntVector(1));
	int32 Changed = 0;
	for (int32 Cz = C0.Z; Cz <= C1.Z; ++Cz)
	{
		for (int32 Cy = C0.Y; Cy <= C1.Y; ++Cy)
		{
			for (int32 Cx = C0.X; Cx <= C1.X; ++Cx)
			{
				const FIntVector Key(Cx, Cy, Cz);
				TUniquePtr<FDCVoxelChunk>* Found = Chunks.Find(Key);
				if (!Found && bSkipEmpty)
				{
					continue;
				}
				FDCVoxelChunk* C = Found ? Found->Get() : nullptr;
				const FIntVector Base = Key * S;
				const FIntVector Lo(FMath::Max(Min.X, Base.X), FMath::Max(Min.Y, Base.Y), FMath::Max(Min.Z, Base.Z));
				const FIntVector Hi(FMath::Min(Max.X, Base.X + S), FMath::Min(Max.Y, Base.Y + S), FMath::Min(Max.Z, Base.Z + S));
				int32 ChunkChanged = 0;
				for (int32 Z = Lo.Z; Z < Hi.Z; ++Z)
				{
					for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
					{
						for (int32 X = Lo.X; X < Hi.X; ++X)
						{
							const int32 I = FDCVoxelChunk::Index(X - Base.X, Y - Base.Y, Z - Base.Z);
							const uint8 Old = C ? C->V[I] : 0;
							const uint8 New = Fn(FIntVector(X, Y, Z), Old);
							if (New == Old)
							{
								continue;
							}
							if (!C)
							{
								C = Chunks.Add(Key, MakeUnique<FDCVoxelChunk>()).Get();
							}
							C->Solid += (New != 0 ? 1 : 0) - (Old != 0 ? 1 : 0);
							C->V[I] = New;
							++ChunkChanged;
						}
					}
				}
				if (ChunkChanged > 0)
				{
					MarkDirtyWithNeighbours(Key);
					Changed += ChunkChanged;
				}
			}
		}
	}
	return Changed;
}
