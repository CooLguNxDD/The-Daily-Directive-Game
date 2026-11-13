// Destruction (R3): carve, structure pass and rubble settle across the sector's grids (10 cm towers, 50 cm terrain).
// Integer cm and voxel math only, no map iteration, no RNG state: every machine that applies the same ops in the same
// order to the same crafted city gets the same voxels. Rubble is committed when the op applies; the fall is cosmetic.
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelTypes.h"

/** A piece that broke off a tower: its voxels (already removed from the tower grid), for the cosmetic fall. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelPiece
{
	int32 Lot = INDEX_NONE;
	/** Toppled (leans over away from the blast) or dropped (an island that lost its support). */
	bool bTopple = false;
	/** Hinge of the topple in tower-grid voxels: the break layer's edge on the fall side. */
	FIntVector Pivot = FIntVector::ZeroValue;
	/** Fall direction on one axis (+-X or +-Y). */
	FIntVector Dir = FIntVector::ZeroValue;
	/** Tower-grid cells and their materials. */
	TArray<FIntVector> Cells;
	TArray<uint8> Materials;
	/** Range of this piece's rubble in FDCVoxelOpResult::Rubble (terrain cells). */
	int32 RubbleStart = 0;
	int32 RubbleNum = 0;
};

struct EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult
{
	/** Lots whose tower broke at a layer and toppled (the host turns these into collapse records). */
	TArray<int32> FallenLots;
	/** Lots whose grid changed (remesh). */
	TArray<int32> TouchedLots;
	TArray<FDCVoxelPiece> Pieces;
	/** Terrain cells the pieces settled into (already written to the terrain grid by Apply). */
	TArray<FIntVector> Rubble;
	TArray<uint8> RubbleMaterials;
	int32 Carved = 0;
};

/** Settings the ops read, resolved once. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelOpContext
{
	const FDCVoxelTuning* Tuning = nullptr;
	const TArray<FDCVoxelCarveDef>* Carves = nullptr;
	int32 BayCm = 300;
	/** Palette material id -> the id it lands as in rubble (index = id). */
	TArray<uint8> RubbleOf;

	const FDCVoxelCarveDef& CarveFor(FName Stratagem) const;
};

/** The sector's voxel state: what the ops change. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelSector
{
	TArray<FDCVoxelTower> Towers;
	FDCVoxelGrid Terrain;
	/** Content hash over every grid (determinism evidence: host and clients compare after each op). */
	uint32 Hash() const;
};

namespace DCVoxelOps
{
	/** Removes every solid voxel of a grid within RadiusCm of CenterCm (voxel centres, integer). Returns how many. */
	EPIC_AI_MEGA_JAM2_API int32 CarveSphere(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm);
	/** Removes a vertical cylinder from the ground up to the grid's top. */
	EPIC_AI_MEGA_JAM2_API int32 CarveColumn(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 TopZ);
	/** Bites the base of a tower on the side facing FromCm: BiteFloors tall, BitePercent of the footprint deep, ragged. */
	EPIC_AI_MEGA_JAM2_API int32 Bite(FDCVoxelTower& Tower, const FIntVector& FromCm, int32 BiteFloors, int32 BitePercent, int32 BayCm);

	/** Structure pass on one tower: the highest layer under SupportPercent of its intact count breaks and everything
	 *  above it topples along Dir; then any island not connected to the ground drops. Removes pieces from the grid. */
	EPIC_AI_MEGA_JAM2_API void Structure(FDCVoxelTower& Tower, const FIntVector& Dir, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out);

	/** Settles a piece into the terrain grid: downsampled to terrain cells in first-seen order, drifted by height,
	 *  slid to the angle of repose, stacked on rubble and stumps, never into a standing tower. Writes the terrain. */
	EPIC_AI_MEGA_JAM2_API void Settle(FDCVoxelSector& Sector, FDCVoxelPiece& Piece, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out);

	/** One replicated op. Strike: beam column + blast sphere in every tower it reaches, a crater in the terrain, a
	 *  bite in every standing tower within the blast radius, then the structure pass and settle. Fell: bite one lot. */
	EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult Apply(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx);

	/** Deterministic per-voxel hash (no RNG state, order independent). */
	EPIC_AI_MEGA_JAM2_API uint32 Hash(const FIntVector& P, uint32 Salt);
}
