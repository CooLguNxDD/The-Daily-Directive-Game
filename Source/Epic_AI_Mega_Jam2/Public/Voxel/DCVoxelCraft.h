// City crafting from data (R2): recipes + sector layout + the seeded lot plan become voxel towers, each in its own
// grid, plus one sector terrain grid (ground, streets, later the rubble). Pure and deterministic.
#pragma once

#include "CoreMinimal.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelGrid.h"
#include "Voxel/DCVoxelTypes.h"

/** A crafted tower in its own grid, with the solid count of every layer when intact (the structure pass compares). */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelTower
{
	int32 Lot = INDEX_NONE;
	FDCVoxelGrid Grid;
	/** Footprint box in grid-local voxels (walls only), Min inclusive, Max exclusive, z from 0. */
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Max = FIntVector::ZeroValue;
	/** Box the structure pass scans: footprint plus trim margin, up to the roof top. */
	FIntVector ScanMin = FIntVector::ZeroValue;
	FIntVector ScanMax = FIntVector::ZeroValue;
	TArray<int32> IntactLayers;
	bool bFallen = false;
	/** Local layer where it broke (the stump top) once fallen. */
	int32 BreakZ = 0;

	/** Footprint and top in sector cm (rubble settles against it). */
	FIntVector FootMinCm() const { return Grid.MinCm(Min); }
	FIntVector FootMaxCm() const { return Grid.MinCm(Max); }
	int32 TopCm() const { return Grid.OriginCm.Z + (bFallen ? BreakZ + 1 : ScanMax.Z) * Grid.VoxelCm; }
};

/** Everything crafting needs, resolved from settings once. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelCraftContext
{
	const TArray<FDCVoxelMaterial>* Palette = nullptr;
	const TArray<FDCVoxelRecipe>* Recipes = nullptr;
	const FDCVoxelTuning* Tuning = nullptr;
	/** One bay and one floor (City.BlockSizeCm). */
	int32 BayCm = 300;

	uint8 MaterialId(FName Id) const;
	const FDCVoxelRecipe* Recipe(int32 Index) const;
};

namespace DCVoxelCraft
{
	/** Picks each lot's recipe from the sector layout and rolls footprint and floors inside the recipe's ranges, from
	 *  the lot's own seed. Gameplay (photo framing, enemies) keeps reading the spec, which now matches the voxels. */
	EPIC_AI_MEGA_JAM2_API void ApplyRecipes(FDCCityPlan& Plan, const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, int32 MaxFitBays);

	/** Crafts one tower at its lot, in a new grid of Tuning.TowerVoxelCm. */
	EPIC_AI_MEGA_JAM2_API void CraftTower(FDCVoxelTower& Out, const FDCLotPlan& Lot, int32 LotIndex, const FDCVoxelCraftContext& Ctx);

	/** The sector terrain: ground slab (street, lot paving, kerbs) at Tuning.TerrainVoxelCm, top surface at z = 0. */
	EPIC_AI_MEGA_JAM2_API void CraftTerrain(FDCVoxelGrid& Out, const FDCCityPlan& Plan, float LotSizeCm, const FDCVoxelCraftContext& Ctx);

	/** Crafts every tower of the plan. Same plan and data, same voxels on every machine. */
	EPIC_AI_MEGA_JAM2_API void CraftCity(const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx, TArray<FDCVoxelTower>& OutTowers);

	/** Solid voxels of one layer inside a box. */
	EPIC_AI_MEGA_JAM2_API int32 CountLayer(const FDCVoxelWorld& World, const FIntVector& Min, const FIntVector& Max, int32 Z);
}
