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
	TMap<FIntVector, TUniquePtr<FDCVoxelChunk>> Chunks;
	TSet<FIntVector> Dirty;
};
