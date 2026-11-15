// Voxel models made by voxel-forge (R17): text .dcvox files under Content/DC/Voxel/, parsed and meshed at run time.
// Parse is pure (tests run it); the cache reads files from the project content directory (staged as non-UFS files).
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelMesher.h"

/** R20: an animation segment of a model (voxel-forge `part`): solid voxels in [Min, Max) move with Role about Joint. */
struct FDCVoxelPart
{
	FName Role;
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Max = FIntVector::ZeroValue;
	/** Joint pivot in voxel corner units (same space as Pivot). */
	FVector Joint = FVector::ZeroVector;
};

/** R20: a unit's mesh with its animation segments. UV1 = (segment, joint x cm), UV2 = (joint y cm, joint z cm), joint
 *  relative to the model pivot like the positions. Segment 0 is the root body (no joint); segment k is Roles[k - 1]. */
struct FDCVoxelUnitMesh
{
	FDCVoxelMeshData Mesh;
	TArray<FVector2D> UV1;
	TArray<FVector2D> UV2;
	TArray<FName> Roles;
};

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
	/** R20 animation segments, in file order (a later part wins where boxes overlap). */
	TArray<FDCVoxelPart> Parts;
	/** Most animated segments a unit material reads (custom primitive data: 4 floats each after the dissolve slot). */
	static constexpr int32 MaxSegments = 7;

	/** Parses the voxel-forge text format. Returns false with a reason on any malformed line (data from disk is untrusted). */
	static bool Parse(const FString& Text, FDCVoxelModel& Out, FString& Error);

	/** Greedy mesh in cm around the pivot, coloured from the game palette (slot names resolved by name). */
	void BuildMesh(const TArray<FName>& PaletteNames, const TArray<FColor>& PaletteColors, FDCVoxelMeshData& Out) const;

	/** R20: distinct part roles in first-seen order; segment k (1-based) animates Roles[k - 1]. */
	TArray<FName> SegmentRoles() const;
	/** R20: the part owning a voxel (the last part whose box holds it), or INDEX_NONE for the root body. */
	int32 PartOf(const FIntVector& Cell) const;
	/** R20: meshes each part on its own (cut faces at the joints get caps) and tags vertices with segment and joint. */
	void BuildUnitMesh(const TArray<FName>& PaletteNames, const TArray<FColor>& PaletteColors, FDCVoxelUnitMesh& Out) const;
};

namespace DCVoxelModels
{
	/** The mesh of a model by name, loaded and meshed on first use (game thread); nullptr when missing or bad. */
	EPIC_AI_MEGA_JAM2_API const FDCVoxelMeshData* Mesh(FName Model);
	/** The parsed model by name (cached, game thread); nullptr when missing or malformed. */
	EPIC_AI_MEGA_JAM2_API const FDCVoxelModel* Parsed(FName Model);
	/** R20: the segmented unit mesh by name (cached, game thread); nullptr when missing or bad. */
	EPIC_AI_MEGA_JAM2_API const FDCVoxelUnitMesh* UnitMesh(FName Model);
}
