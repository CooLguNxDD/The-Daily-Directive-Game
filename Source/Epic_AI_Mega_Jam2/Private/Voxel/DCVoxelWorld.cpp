#include "Voxel/DCVoxelWorld.h"

void FDCVoxelWorld::Set(const FIntVector& P, uint8 Material)
{
	const FIntVector Key = ChunkOf(P);
	TUniquePtr<FDCVoxelChunk>* Found = Chunks.Find(Key);
	if (!Found)
	{
		if (Material == 0)
		{
			return;
		}
		Found = &Chunks.Add(Key, MakeUnique<FDCVoxelChunk>());
	}
	FDCVoxelChunk& C = **Found;
	const FIntVector L = LocalOf(P);
	uint8& Cell = C.V[FDCVoxelChunk::Index(L.X, L.Y, L.Z)];
	if (Cell == Material)
	{
		return;
	}
	C.Solid += (Material != 0 ? 1 : 0) - (Cell != 0 ? 1 : 0);
	Cell = Material;
	Dirty.Add(Key);
	// A border voxel changes the neighbour's faces and ambient occlusion too.
	const int32 Last = FDCVoxelChunk::Mask;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (L[Axis] == 0 || L[Axis] == Last)
		{
			FIntVector N = Key;
			N[Axis] += L[Axis] == 0 ? -1 : 1;
			if (Chunks.Contains(N))
			{
				Dirty.Add(N);
			}
		}
	}
}

void FDCVoxelWorld::FillBox(const FIntVector& Min, const FIntVector& Max, uint8 Material)
{
	for (int32 Z = Min.Z; Z < Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y < Max.Y; ++Y)
		{
			for (int32 X = Min.X; X < Max.X; ++X)
			{
				Set(FIntVector(X, Y, Z), Material);
			}
		}
	}
}

int64 FDCVoxelWorld::CountSolid() const
{
	int64 N = 0;
	for (const TPair<FIntVector, TUniquePtr<FDCVoxelChunk>>& Pair : Chunks)
	{
		N += Pair.Value->Solid;
	}
	return N;
}

uint32 FDCVoxelWorld::ContentHash() const
{
	// Sum of per-chunk hashes: independent of map iteration order. Empty chunks do not count.
	uint32 Sum = 0;
	for (const TPair<FIntVector, TUniquePtr<FDCVoxelChunk>>& Pair : Chunks)
	{
		if (Pair.Value->Solid == 0)
		{
			continue;
		}
		const uint32 H = FCrc::MemCrc32(Pair.Value->V, sizeof(Pair.Value->V), GetTypeHash(Pair.Key));
		Sum += H;
	}
	return Sum;
}

void FDCVoxelWorld::CopyPadded(const FIntVector& ChunkKey, TArray<uint8>& Out) const
{
	constexpr int32 S = FDCVoxelChunk::Size;
	constexpr int32 P = S + 2;
	Out.SetNumZeroed(P * P * P);
	const FIntVector Base = ChunkKey * S - FIntVector(1);
	// Fast path for the chunk's own interior, slow path (Get) for the one-voxel border.
	const FDCVoxelChunk* Own = FindChunk(ChunkKey);
	for (int32 Z = 0; Z < P; ++Z)
	{
		for (int32 Y = 0; Y < P; ++Y)
		{
			const bool bInnerYZ = Y > 0 && Y < P - 1 && Z > 0 && Z < P - 1;
			uint8* Row = Out.GetData() + (Z * P + Y) * P;
			if (bInnerYZ && Own)
			{
				FMemory::Memcpy(Row + 1, &Own->V[FDCVoxelChunk::Index(0, Y - 1, Z - 1)], S);
				Row[0] = Get(Base + FIntVector(0, Y, Z));
				Row[P - 1] = Get(Base + FIntVector(P - 1, Y, Z));
				continue;
			}
			if (bInnerYZ)
			{
				Row[0] = Get(Base + FIntVector(0, Y, Z));
				Row[P - 1] = Get(Base + FIntVector(P - 1, Y, Z));
				continue;
			}
			for (int32 X = 0; X < P; ++X)
			{
				Row[X] = Get(Base + FIntVector(X, Y, Z));
			}
		}
	}
}
