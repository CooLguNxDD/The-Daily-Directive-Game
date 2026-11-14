// Voxel engine data: palette, building recipes, sector layouts, carve definitions and engine tuning.
// Every row lives in Config/DefaultGame.ini [/Script/Epic_AI_Mega_Jam2.DCVoxelSettings]; code reads, never names, them.
#pragma once

#include "CoreMinimal.h"
#include "DCVoxelTypes.generated.h"

/** One palette entry. Voxel material id = index in the palette + 1 (0 is air). */
USTRUCT(BlueprintType)
struct FDCVoxelMaterial
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor Color = FLinearColor(0.1f, 0.1f, 0.1f);
	/** Emissive strength (0..1, scaled by the voxel material's GlowScale): lit windows. Nothing glows like a blast. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float Glow = 0.f;
	/** Material a voxel of this kind turns into when it lands as rubble (None = keeps its own). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName RubbleAs;
};

/** A building recipe: the tower is crafted from these rows, seeded per lot (R2). Sizes are in voxels or bays. */
USTRUCT(BlueprintType)
struct FDCVoxelRecipe
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Id;
	/** Footprint per axis in bays (one bay = one floor height, City.BlockSizeCm). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 MinBays = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 MaxBays = 5;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 MinFloors = 6;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 MaxFloors = 16;
	/** Wall thickness (cm). All recipe sizes are cm, so a recipe keeps its look at any voxel size. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WallCm = 20;
	/** Facade bay pattern for upper floors, cycled around each face: W window, S slot window, G paired windows, P solid pier. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FString Facade = TEXT("W");
	/** Ground floor pattern: D door, P pier, W shop window. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FString Base = TEXT("PD");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowSillCm = 90;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowHeightCm = 150;
	/** Floors between setbacks (0 = straight tower) and how far each setback steps in per side (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SetbackEvery = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SetbackCm = 0;
	/** Roof builder id resolved by the roof registry: Flat, Crown, Spire, Antenna. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Roof = TEXT("Flat");
	/** Horizontal band (string course) every N floors, in Trim (0 = none). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BandEvery = 0;
	/** Chance a face carries a paper poster (cream, never red: red is danger only). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PosterChance = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Wall = TEXT("Ink");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Window = TEXT("Window");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Trim = TEXT("Trim");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Slab = TEXT("Slab");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Poster = TEXT("Paper");
};

USTRUCT(BlueprintType)
struct FDCVoxelRecipeWeight
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Recipe;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float Weight = 1.f;
};

/** Sector layout (R2): which recipes a sector draws from. The lot grid itself is City tuning. */
USTRUCT(BlueprintType)
struct FDCVoxelSectorLayout
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") TArray<FDCVoxelRecipeWeight> Recipes;
};

/** How a strike carves (R3). Radii in cm. Towers inside the stratagem's blast radius lose a bite of their base
 *  on the side facing the blast: BiteFloors tall, BitePercent of the footprint deep, so the structure pass drops them. */
USTRUCT(BlueprintType)
struct FDCVoxelCarveDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Stratagem;
	/** The beam column punched straight down through anything at the strike point. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ColumnRadiusCm = 250;
	/** The blast sphere carved into towers. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SphereRadiusCm = 550;
	/** The crater punched into the street (terrain grid). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CraterRadiusCm = 300;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BiteFloors = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BitePercent = 70;
};

/** Engine tuning (R1). Integer where the result must match across machines. */
USTRUCT(BlueprintType)
struct FDCVoxelTuning
{
	GENERATED_BODY()
	/** Voxel size per object grid (R1), integer cm: tower shells, sector terrain (ground and rubble), props and models. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 TowerVoxelCm = 10;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 TerrainVoxelCm = 50;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PropVoxelCm = 5;
	/** Terrain: ground layers below the street surface, and how far the ground reaches past the outer streets. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 GroundLayers = 2;
	/** Bottom terrain layers no crater carves (the street never opens onto the void). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BedrockLayers = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 TerrainMarginCm = 2000;
	/** Render cell edge (cm): storage chunks are grouped into one mesh component per cell. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 RenderCellCm = 960;
	/** Ground materials by surface: street, lot paving, kerb line. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName StreetMaterial = TEXT("Asphalt");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName LotMaterial = TEXT("Paving");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName KerbMaterial = TEXT("Kerb");
	/** A layer of a tower stands while it keeps this percent of its intact solid voxels; below it everything above falls. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SupportPercent = 45;
	/** Island connectivity runs on blocks of this many voxels per edge; a block holds weight only with at least
	 *  IslandBlockFill solid voxels (thin links break). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 IslandBlockVoxels = 5;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 IslandBlockFill = 18;
	/** Percent of a fallen piece's voxels that land as rubble (the rest is dust). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 RubbleKeepPercent = 35;
	/** How far a toppling piece is thrown: voxels of drift per 100 voxels of height above the break. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ToppleDriftPercent = 55;
	/** Rubble angle of repose: a terrain voxel slides to a neighbour column more than this many voxels lower. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ReposeStep = 2;
	/** Seconds a falling piece takes to hit the street (cosmetic; the settled rubble is decided at the op). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FallSeconds = 1.6f;
	/** Two-phase strike ops: the host appends a strike's op this long before detonation (every machine plans it on
	 *  the worker), and each city commits it at the detonation time, crater and collapse records in that frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PlanLeadSeconds = 0.8f;
	/** Fx puffs closer than FxNearHideCm to the local camera are hidden, and grow back over FxNearFadeCm. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FxNearHideCm = 150.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FxNearFadeCm = 350.f;
	/** Photo sight march: step along the ray (cm), and the stretch at the lens and before the subject that never blocks. */
	/** Around a planned op, street tops are served from a snapshot until it commits: blast radius plus this (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float HoldReachCm = 1500.f;
	/** City frame stages longer than this (ms) are logged as DCHITCH (perf evidence). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float HitchLogMs = 20.f;
	/** Voxel readability in M_DCVoxel: edge bevel width (fraction of a voxel), its darkening, and per-voxel jitter. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float VoxelEdgeWidth = 0.08f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float VoxelEdgeDark = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float VoxelJitter = 0.12f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SightStepCm = 25.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SightSkipNearCm = 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SightSkipFarCm = 250.f;
	/** Gravity of the cosmetic fall (cm/s^2, exaggerated so a collapse reads at a glance). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FallGravityCm = 2400.f;
	/** Degrees a toppling piece leans before it breaks apart (cosmetic). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ToppleDegrees = 38.f;
	/** Chunk mesh uploads per frame (time slicing on the game thread; meshing itself runs on worker threads). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 UploadsPerFrame = 24;
	/** Per-vertex ambient occlusion strength (0 = flat faces). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float AOStrength = 0.38f;
	/** Flying voxel chunks spawned when a piece lands (cosmetic, pooled). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 LandingDebris = 70;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float VoxelMaterialGlowScale = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FDCVoxelCarveDef DefaultCarve;
	/** Decay window: how long the rubble of a fallen piece smokes and burns (s). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DecaySeconds = 9.f;
};

/** District rules for one sector (R16): the street grid, which lot kinds the PCG rules roll, the landmark, the sight
 *  line, and the prop rule set. The city builder turns these into the plan; the crafter turns the plan into voxels. */
USTRUCT(BlueprintType)
struct FDCDistrictRule
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "District") int32 GridX = 4;
	UPROPERTY(EditAnywhere, Config, Category = "District") int32 GridY = 4;
	UPROPERTY(EditAnywhere, Config, Category = "District") float LotSizeCm = 2600.f;
	UPROPERTY(EditAnywhere, Config, Category = "District") float StreetWidthCm = 1600.f;
	/** PCG rule set: slot Lot (kinds: Tower, Low, Plaza, Rubble), LandmarkFloors (range), LowFloors (range),
	 *  LowRecipe (choices). */
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Rules;
	/** PCG rule set: slots Street / StreetCount, Plaza / PlazaCount, Rubble / RubbleCount (model choices and ranges). */
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Props;
	UPROPERTY(EditAnywhere, Config, Category = "District") FName LandmarkRecipe = TEXT("Landmark");
	/** Lot kind forced along the sight line from the spawn street to the landmark (open or low, so it stays in view). */
	UPROPERTY(EditAnywhere, Config, Category = "District") FName SightLineKind = TEXT("Low");
	/** PCG rule set with slot Season: the sector's season (and so its weather arcs and snow), rolled per run (R18). */
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Weather;
	/** Never fewer towers than this (a quota needs things to knock down). */
	UPROPERTY(EditAnywhere, Config, Category = "District") int32 MinTowers = 8;
};

/** A puff kind for the decay effects (R4): a chunky voxel cube that moves, spins, grows or shrinks and dies. */
USTRUCT(BlueprintType)
struct FDCFxKind
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Fx") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") FLinearColor Color = FLinearColor(0.3f, 0.28f, 0.25f);
	/** Emissive multiplier (fire and embers only; blast white-orange is for blasts). */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Glow = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") int32 Pool = 200;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float LifeMin = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float LifeMax = 2.f;
	/** Cube edge (cm) at birth and at death. */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float SizeStart = 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float SizeEnd = 120.f;
	/** Outward speed and upward speed at birth (cm/s); gravity (cm/s^2, negative = falls); drag per second. */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Speed = 200.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Rise = 100.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Gravity = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Drag = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float SpinDeg = 60.f;
	/** Height multiplier (rain streaks). */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Stretch = 1.f;
};

USTRUCT(BlueprintType)
struct FDCFxBurst
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Fx") FName Kind;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") int32 Count = 10;
};

/** A named effect moment: what puffs spawn, and for burns how often (seconds between bursts). */
USTRUCT(BlueprintType)
struct FDCFxEmitter
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Fx") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") TArray<FDCFxBurst> Bursts;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") float Interval = 0.25f;
};

/** One replicated voxel operation (R3): the host appends, every machine applies in Seq order. Positions are integer
 *  cm, quantized once on the host, so every machine carves exactly the same voxels in every grid. */
USTRUCT(BlueprintType)
struct FDCVoxelOp
{
	GENERATED_BODY()
	UPROPERTY() int32 Seq = 0;
	/** Which city build the op belongs to (GameState CityBuildId): a rebuilt sector starts clean. */
	UPROPERTY() int32 Build = 0;
	UPROPERTY() int32 Sector = 0;
	/** Server time the op happened (the reel replays it at this content time). */
	UPROPERTY() float Time = 0.f;
	/** Strike (carve at Center + bite towers within BlastRadius) or Fell (bite one lot's base from Center's side). */
	UPROPERTY() FName Kind = TEXT("Strike");
	/** Sector-local integer cm. */
	UPROPERTY() FIntVector CenterCm = FIntVector::ZeroValue;
	UPROPERTY() int32 BlastRadiusCm = 0;
	UPROPERTY() FName Stratagem;
	UPROPERTY() int32 Lot = -1;
	/** Host's voxel content hash after applying this op: clients compare theirs and log a mismatch (R3 evidence). */
	UPROPERTY() uint32 ResultHash = 0;
};
