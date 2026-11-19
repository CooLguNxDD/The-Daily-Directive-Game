// One object's voxel grid (R1): its own voxel size (integer cm) and origin. Each tower, the sector terrain (ground
// and settled rubble) and each prop or model has its own grid. All simulation positions are integer cm.
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelWorld.h"

namespace DCVoxelMath
{
	/** Floor division that rounds toward minus infinity (negative coordinates stay on the right cell). */
	inline int32 FloorDiv(int32 A, int32 B)
	{
		const int32 Q = A / B;
		return (A % B != 0 && ((A < 0) != (B < 0))) ? Q - 1 : Q;
	}
	inline FIntVector FloorDiv(const FIntVector& A, int32 B)
	{
		return FIntVector(FloorDiv(A.X, B), FloorDiv(A.Y, B), FloorDiv(A.Z, B));
	}
	inline FIntVector ToCm(const FVector& V)
	{
		return FIntVector(FMath::RoundToInt32(V.X), FMath::RoundToInt32(V.Y), FMath::RoundToInt32(V.Z));
	}
}

struct EPIC_AI_MEGA_JAM2_API FDCVoxelGrid
{
	/** Sector-local cm of the min corner of voxel (0,0,0). */
	FIntVector OriginCm = FIntVector::ZeroValue;
	int32 VoxelCm = 10;
	FDCVoxelWorld Data;

	FIntVector ToLocal(const FIntVector& Cm) const { return DCVoxelMath::FloorDiv(Cm - OriginCm, VoxelCm); }
	FIntVector MinCm(const FIntVector& Local) const { return OriginCm + Local * VoxelCm; }
	FIntVector CenterCm(const FIntVector& Local) const { return OriginCm + Local * VoxelCm + FIntVector(VoxelCm / 2); }
	FVector ToWorld(const FIntVector& Local) const { return FVector(MinCm(Local)); }
};
