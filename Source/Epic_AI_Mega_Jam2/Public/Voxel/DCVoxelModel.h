// Voxel models made by voxel-forge (R17): text .dcvox files under Content/DC/Voxel/, parsed and meshed at run time.
// Parse is pure (tests run it); the cache reads files from the project content directory (staged as non-UFS files).
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelMesher.h"

struct EPIC_AI_MEGA_JAM2_API FDCVoxelModel
{
	FName Name;
	int32 GridCm = 5;
	FIntVector Size = FIntVector::ZeroValue;
	FIntVector Pivot = FIntVector::ZeroValue;
	/** Palette material names, slot i + 1 in Cells. */
	TArray<FName> Slots;
	/** Slot per voxel, x fastest, then y, then z (0 = air). */
	TArray<uint8> Cells;

	/** Parses the voxel-forge text format. Returns false with a reason on any malformed line (data from disk is untrusted). */
	static bool Parse(const FString& Text, FDCVoxelModel& Out, FString& Error);

	/** Greedy mesh in cm around the pivot, coloured from the game palette (slot names resolved by name). */
	void BuildMesh(const TArray<FName>& PaletteNames, const TArray<FColor>& PaletteColors, FDCVoxelMeshData& Out) const;
};

namespace DCVoxelModels
{
	/** The mesh of a model by name, loaded and meshed on first use (game thread); nullptr when missing or bad. */
	EPIC_AI_MEGA_JAM2_API const FDCVoxelMeshData* Mesh(FName Model);
}
