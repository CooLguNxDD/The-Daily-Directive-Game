// City crafting from data (R2): recipes + sector layout + the seeded lot plan become voxel towers, each in its own
// grid, plus one sector terrain grid (ground, streets, later the rubble). Pure and deterministic.
#pragma once

#include "CoreMinimal.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelGrid.h"
#include "Voxel/DCVoxelTypes.h"
#include "Rules/DCPcg.h"

/** A lit sign on a facade (R25 neon): its centre voxel one voxel proud of the wall (grid-local), the outward normal,
 *  its size in voxels and its palette material. The city hangs a pooled shadowless rect light on the nearest ones. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelSign
{
	FIntVector Center = FIntVector::ZeroValue;
	/** A lit (non-emblem) voxel of the sign: carved away = the sign's light dies (the centre is the dark emblem). */
	FIntVector Probe = FIntVector::ZeroValue;
	FIntVector Normal = FIntVector::ZeroValue;
	int32 WidthVox = 0;
	int32 HeightVox = 0;
	uint8 Material = 0;
	/** Grid-local box of every sign voxel (Min inclusive, Max exclusive) and the wall box right behind it: the
	 *  structure pass drops the sign when too little of that wall is left (SignHoldPercent). */
	FIntVector BoxMin = FIntVector::ZeroValue;
	FIntVector BoxMax = FIntVector::ZeroValue;
	FIntVector BackMin = FIntVector::ZeroValue;
	FIntVector BackMax = FIntVector::ZeroValue;
	int32 BackCells = 0;
};

/** Where a building attachment (R2 props) landed: its model, the centre of its box in sector cm and the way it faces
 *  (unit axis). The city hangs a prop light in front of it when the model has a PropLights row. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelAttachSpot
{
	FName Model;
	FIntVector CenterCm = FIntVector::ZeroValue;
	FIntVector Normal = FIntVector::ZeroValue;
};

/** A crafted tower in its own grid, with the solid count of every layer when intact (the structure pass compares). */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelTower
{
	/** Lit signs crafted on its facades (lights; the structure pass drops a sign whose wall is gone). */
	TArray<FDCVoxelSign> Signs;
	/** Roof clutter and wall mounts stamped into this tower (R2). */
	TArray<FDCVoxelAttachSpot> AttachSpots;
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
struct FDCVoxelModel;

struct EPIC_AI_MEGA_JAM2_API FDCVoxelCraftContext
{
	/** voxel-forge models by name for props (loaded on the game thread before crafting). */
	TMap<FName, const FDCVoxelModel*> Models;
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

	/** The city builder (R16): lays out a sector from its district rule with the PCG rule sets (R15): street grid, lot
	 *  kinds (towers, low blocks, plazas, rubble zones), the landmark, the sight line from the spawn street to it, and
	 *  props on streets, plazas and rubble. Pure: same seed and rows, same plan. */
	EPIC_AI_MEGA_JAM2_API FDCCityPlan PlanDistrict(int32 Seed, const FDCDistrictRule& District, const FDCPcgRuleSet* LotRules, const FDCPcgRuleSet* PropRules,
		const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, float BayCm, const TArray<FDCPropLayer>& PropLayers);

	/** Every voxel-forge model a plan uses (props and building attachments): load these into the context first. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> PlanModels(const FDCCityPlan& Plan);

	/** Stamps a model into a grid of TargetCm voxels with its pivot at Base, in quarter turns (a coarser model scales
	 *  up, a finer one is sampled). bOnlyAir keeps what is already there. Returns the top z written + 1 (Base.Z when
	 *  nothing). */
	EPIC_AI_MEGA_JAM2_API int32 StampModel(FDCVoxelWorld& W, const FDCVoxelModel& M, int32 TargetCm, const FIntVector& Base, int32 Turns,
		const FDCVoxelCraftContext& Ctx, bool bOnlyAir);

	/** Stamps the plan's props into a prop grid (Tuning.PropVoxelCm), in quarter turns, from the loaded models, and
	 *  strings its cables. */
	EPIC_AI_MEGA_JAM2_API void CraftProps(FDCVoxelGrid& Out, const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx);

	/** Crafts one tower at its lot, in a new grid of Tuning.TowerVoxelCm. */
	EPIC_AI_MEGA_JAM2_API void CraftTower(FDCVoxelTower& Out, const FDCLotPlan& Lot, int32 LotIndex, const FDCVoxelCraftContext& Ctx);

	/** The sector terrain: ground slab (street, lot paving, kerbs) at Tuning.TerrainVoxelCm, top surface at z = 0. */
	EPIC_AI_MEGA_JAM2_API void CraftTerrain(FDCVoxelGrid& Out, const FDCCityPlan& Plan, float LotSizeCm, const FDCVoxelCraftContext& Ctx);

	/** Pre-ruined lots: rubble mounds in the terrain grid. */
	EPIC_AI_MEGA_JAM2_API void CraftRubbleZones(FDCVoxelGrid& Out, const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx);

	/** Crafts every tower of the plan. Same plan and data, same voxels on every machine. */
	EPIC_AI_MEGA_JAM2_API void CraftCity(const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx, TArray<FDCVoxelTower>& OutTowers);

	/** Solid voxels of one layer inside a box. */
	/** R23 data checks: a recipe's Roof, SignKind and every Facade/Base bay char resolve in the craft registries. */
	EPIC_AI_MEGA_JAM2_API bool IsRoofKind(FName Roof);
	EPIC_AI_MEGA_JAM2_API bool IsSignKind(FName Sign);
	EPIC_AI_MEGA_JAM2_API bool IsBayKind(TCHAR Bay);

	EPIC_AI_MEGA_JAM2_API int32 CountLayer(const FDCVoxelWorld& World, const FIntVector& Min, const FIntVector& Max, int32 Z);
}
