// Greedy voxel mesher (R1). Pure: reads a padded copy of one chunk, writes plain arrays, so it runs on a worker thread.
#pragma once

#include "CoreMinimal.h"

/** Palette colours for the mesher: sRGB colour per material id, alpha = glow (0..255). Index = material id. */
using FDCVoxelColors = TArray<FColor>;

struct EPIC_AI_MEGA_JAM2_API FDCVoxelMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<int32> Indices;

	bool IsEmpty() const { return Indices.Num() == 0; }
	void Reset() { Positions.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Indices.Reset(); }
};

namespace DCVoxelMesher
{
	/** Meshes the 32^3 interior of a 34^3 padded block (one-voxel border from the neighbours, x fastest).
	 *  Faces merge greedily when material and corner occlusion match. Positions are chunk-local cm. */
	EPIC_AI_MEGA_JAM2_API void MeshPadded(const TArray<uint8>& Padded, const FDCVoxelColors& Colors, float VoxelSizeCm, float AOStrength, FDCVoxelMeshData& Out);

	/** Meshes a loose set of voxels (a falling piece): every exposed face, no greedy merge. Positions relative to Origin. */
	EPIC_AI_MEGA_JAM2_API void MeshCells(const TArray<FIntVector>& Cells, const TArray<uint8>& Materials, const FIntVector& Origin, const FDCVoxelColors& Colors, float VoxelSizeCm, FDCVoxelMeshData& Out);
}
