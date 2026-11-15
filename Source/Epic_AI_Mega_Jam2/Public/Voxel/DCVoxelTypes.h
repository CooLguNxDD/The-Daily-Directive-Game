// Voxel engine data: palette, building recipes, sector layouts, carve definitions and engine tuning.
// Every row lives in Config/DefaultGame.ini [/Script/Epic_AI_Mega_Jam2.DCVoxelSettings]; code reads, never names, them.
#pragma once

#include "CoreMinimal.h"
#include "DCVoxelTypes.generated.h"

/** One palette entry. Voxel material id = index in the palette + 1 (0 is air). */
/** R25 prop light pools (advisor a-5): a prop model that casts light on the wet street when the camera is near: lamps
 *  warm, signs, kiosks and billboards magenta or cyan. The nearest LampLights spots of any row get a pooled shadowless
 *  point light; a blast that reaches one puts it out. */
USTRUCT(BlueprintType)
struct FDCPropLightDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Model;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor Color = FLinearColor(1.f, 0.62f, 0.3f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float Candela = 600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float RadiusCm = 900.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float HeightCm = 250.f;
};

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
	/** Facade bay pattern for upper floors, cycled around each face: W window, S slot window, G paired windows, P solid pier;
	 *  R23: R ribbon window, T twin tall slots, X storefront glass. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FString Facade = TEXT("W");
	/** Ground floor pattern: D door, P pier, W shop window. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FString Base = TEXT("PD");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowSillCm = 90;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowHeightCm = 150;
	/** Floors between setbacks (0 = straight tower) and how far each setback steps in per side (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SetbackEvery = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SetbackCm = 0;
	/** Roof builder id resolved by the roof registry: Flat, Crown, Spire, Antenna; R23 Tiered, Dome, Helipad, Sawtooth, Shanty, Beacon. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Roof = TEXT("Flat");
	/** Horizontal band (string course) every N floors, in Trim (0 = none). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BandEvery = 0;
	/** Chance a face carries a paper poster (cream, never red: red is danger only). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PosterChance = 0.f;
	/** Facade relief (V5.3): a pier rib this wide (cm) stands one voxel proud at the start of every bay and at the
	 *  corners, and a lintel this tall (cm) caps each window one voxel proud, so walls read as stepped voxel shells. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PilasterCm = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 LintelCm = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Wall = TEXT("Ink");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Window = TEXT("Window");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Trim = TEXT("Trim");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Slab = TEXT("Slab");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName Poster = TEXT("Paper");
	/** Window variety per opening (critic: a uniform lit grid reads as a black slab with dots): OffPercent of the
	 *  windows are dark glass (WindowOff), AltPercent use WindowAlt (another tint); hashed per bay and floor. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowOffPercent = 0;
	/** Relief depth in voxels (advisor CP12: 1 voxel of relief is ~2 px at 100 m): string-course ledges and pier ribs
	 *  stand this far proud of the wall (at most 4: the tower's scan margin). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 LedgeDepthVox = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 RibDepthVox = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName WindowOff;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 WindowAltPercent = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName WindowAlt;
	/** The sign's outer ring (a neon tube edge, R25); None = the same as Poster. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName PosterEdge;
	/** Sign builder id from the sign registry (critic V19.4: storefront-scale neon, not giant flat boards): Blade = a
	 *  vertical sign standing out from a pier, Board = a horizontal band over the ground floor, Poster = the old
	 *  two-bay board; R23 Banner = a cloth strip hanging from the roof line, Crown = a band across the top floor. Sizes in voxels: Width along the face (blade thickness), Height, Depth out from the wall. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName SignKind = TEXT("Poster");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignWidthVox = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignHeightVox = 36;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignDepthVox = 6;
	/** Dark glyph strokes across the sign every N voxels (reads as lettering; 0 = none). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignGlyphEvery = 4;
	/** Share of the district's Roof-placer clutter count this recipe takes (R2 props; 0 = none: stepped or spire
	 *  tops have no flat deck). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 RoofPropPercent = 100;
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
	/** Physics detail of this strike's collapse (advisor a-3 step 2): pieces and fragments per piece; -1 = the Tuning
	 *  values. It travels with the op (the stratagem id), so every machine builds the same fragment tree. Faction
	 *  strikes use less so the crew's own shots keep the body budget. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsPieces = -1;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsFragments = -1;
	/** R11 (human playtest): how hard this strike throws the blown-out voxel blocks: outward speed and upward lift as
	 *  multiples of Tuning ShardSpeedCms / ShardUpCms (crumbs too). Speed only: no extra bodies, same budget. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardSpeedScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardLiftScale = 1.f;
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
	/** R19 sector bounds (human playtest: players walked off the map). Each sector's ring sits BoundsMarginCm past its
	 *  own city plan's half extent (so the bounds follow every sector's layout); invisible walls BoundsWallHeightCm tall
	 *  block players, units and physics pieces there (not shots or the camera). The terrain band between the ring and
	 *  the terrain edge is dressed: a barrier line (BarrierHeightCm, BarrierMaterial, a gap-free ring) and seeded rubble
	 *  heaps outside it (EdgeRubbleChance per voxel column, up to EdgeRubbleMaxCm tall). Past the terrain a flat ground
	 *  plane reaches OuterGroundHalfCm in the street colour so no void shows. Anything below KillZCm is returned
	 *  (players to their last safe spot) or released (physics pieces). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BoundsMarginCm = 800;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float BoundsWallHeightCm = 6000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BarrierHeightCm = 100;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName BarrierMaterial = TEXT("Kerb");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float EdgeRubbleChance = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 EdgeRubbleMaxCm = 200;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float OuterGroundHalfCm = 90000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float KillZCm = -1500.f;
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
	/** A sign falls as a piece when less than this share of the wall behind it is left (critic V19.1: no torn sign
	 *  hanging in the air off one corner). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignHoldPercent = 60;
	/** Sector crafts at sector start run on a worker under the sector card (R13 load frames, advisors CP15/CP18). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") bool bAsyncSectorCraft = true;
	/** The reel's restage crafts on a worker and replays the ops before the clip through the op queue while its
	 *  slate is up, instead of one multi-second game-thread frame (R13 reel; advisor CP24, snapshots not built). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") bool bAsyncReelRestage = true;
	/** Form floor in M_DCVoxel (advisor CP16, critic V20.5): shaded albedo x lerp(FormGround, FormSky) by how much a
	 *  face turns to MoonDir, x FormFloor, added as emission, so unlit faces separate without lifting the frame
	 *  (0 = off). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FormFloor = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor FormSky = FLinearColor(0.32f, 0.4f, 0.8f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor FormGround = FLinearColor(0.05f, 0.05f, 0.08f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FVector MoonDir = FVector(-0.4f, 0.3f, 0.85f);
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
	/** Game-thread milliseconds per frame for cell uploads (merge + mesh section); UploadsPerFrame stays a cap. At
	 *  least one cell uploads per frame. A strike's commit frame uploads only that op's cells, outside this budget. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float UploadBudgetMs = 4.f;
	// R1 (advisor a-22, hitch-2324218): op work (the op task, chunk re-meshes, falling-piece meshes) runs on the background task pool so the render thread's own tasks
	// do not queue behind hundreds of mesher jobs (render stalls of 55-125 ms after a strike). Builds stay foreground.
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") bool bOpMeshInBackground = true;
	/** Physics body boxes registered per quiet frame until PhysicsMaxBodies exist (spread, not one 420-box frame). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 BodyPrewarmPerFrame = 24;
	/** Falling-piece mesh sections created per frame stop after this many ms (advisor CP26 #5: ~50 fragment sections
	 *  in one frame cost 25-35 ms); the rest wait for the next frame, a piece shows once all its fragments are in. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PieceUploadBudgetMs = 4.f;
	/** Load phase: after a Build (sector start, a reel clip's rebuild) until every cell is up, the budget is this many
	 *  ms, so the city stands before the clip or the first strike (a 4 ms budget left the reel's city empty). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float UploadLoadBudgetMs = 60.f;
	/** Most render cells uploaded per frame while the sector loads under the card. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 UploadLoadMaxCells = 12;
	/** Load budget for a synchronous build (the reel restage): fast, no cell cap. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float UploadSyncLoadBudgetMs = 60.f;
	/** Per-vertex ambient occlusion strength (0 = flat faces). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float AOStrength = 0.38f;
	/** Flying voxel chunks spawned when a piece lands (cosmetic, pooled). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 LandingDebris = 70;
	/** Sign lights (R25, advisor CP7): the nearest SignLights crafted signs to the local view get a pooled shadowless
	 *  rect light in the sign's colour, facing out; its specular on the wet street is the neon reflection. Windows never
	 *  get lights. Intensity in candela, source = the sign's size times SignSourceScale. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 SignLights = 10;
	/** Aftermath firelight over each fresh rubble pile for DecaySeconds (pooled, shadowless, flickering). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 FireLights = 6;
	/** Street lamps: the nearest LampLights props of model LampModel get a pooled warm shadowless light at LampHeightCm
	 *  (a pool of light on the wet street); a blast that reaches a lamp puts it out. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FName LampModel = TEXT("lamppost");
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 LampLights = 8;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float LampCandela = 900.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float LampRadiusCm = 1400.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float LampHeightCm = 380.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor LampColor = FLinearColor(1.f, 0.62f, 0.3f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FireLightCandela = 1500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FireLightRadiusCm = 1800.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float FireLightHeightCm = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor FireLightColor = FLinearColor(1.f, 0.38f, 0.08f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignLightCandela = 600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignLightRadiusCm = 2600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignLightOffsetCm = 40.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignSourceScale = 0.8f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignLightMaxDistanceCm = 9000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float SignLightRepickSeconds = 0.3f;
	/** Real physics (R26): the N biggest pieces of each op are meshed as fragments (SplitFragments) that fall together
	 *  as one piece (the topple) and break apart into Chaos rigid bodies when it hits the street: box colliders with
	 *  their emissive voxel mesh riding on them, tumbling, colliding with the street, the towers and each other, chipping
	 *  on hard impacts. The rubble every machine stamps is the host op's deterministic rubble; the bodies are local and
	 *  drawn toward it, and swap to it under the landing dust when they sleep. 0 = scripted only (the fallback). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsPieces = 2;
	/** Fragments per piece at most (the block grid doubles until it fits), and live bodies in the sector at most (a
	 *  piece that would pass the cap falls scripted). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsFragments = 16;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsMaxBodies = 360;
	/** Chunk block edge in cm (rounded to the piece's voxels): chunks break off as the piece falls. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsFragmentCm = 400.f;
	/** Leaves: each chunk is made of up to LeavesPerChunk leaves of LeafCm blocks; a hit over ShatterCms (speed lost)
	 *  breaks the chunk into them, bursting apart at ShatterBurstCms. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsLeafCm = 120.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 PhysicsLeavesPerChunk = 6;
	/** Bodies shrink away over this long when their group rests (the shared rubble shows under them). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsFadeSeconds = 0.8f;
	/** A body still flying when its group rests keeps flying up to this long, then fades where it is (on the street). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsStrayFlySeconds = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsShatterCms = 350.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsShatterBurstCms = 250.f;
	/** Chunks never shatter in their first seconds (neighbouring chunks touch as they break off). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsShatterGraceSeconds = 0.3f;
	/** Fraction of the scripted fall (0..1) at which a toppling piece starts breaking into chunks. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsBreakAtU = 0.35f;
	/** Fragments nearer the blast split first: priority = extent / (1 + distance / FocusFalloffCm) (0 = extent only). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsFocusFalloffCm = 0.f;
	/** Shatter (R26): carved voxels fly as up to ShardMax bodies; cluster edge = 1 voxel + 1 per ShardGrowCm from the
	 *  blast centre, at most ShardBlockMax voxels; speed (cm/s) outward, up kick, life (s). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ShardMax = 110;
	/** Physics shards spawned per frame at most (the rest follow next frames; R13 spawn burst). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ShardSpawnsPerFrame = 28;
	/** R6 walk masks: a standing tower's voxel column is walkable when nothing solid sits between WalkBandFromCm and
	 *  WalkBandFromCm + WalkBandHeightCm above its base (a unit's body); open footprint-edge cells are the doors. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float WalkBandFromCm = 20.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float WalkBandHeightCm = 160.f;
	/** Flying leaf pieces cast shadows (off: R13, advisor a-15; chunks always cast). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") bool PhysicsLeafShadows = false;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardGrowCm = 150.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ShardBlockMax = 4;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardSpeedCms = 850.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardUpCms = 450.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardLifeSeconds = 8.f;
	/** Shards keep this share of their palette glow (lit windows still glow in the air, but never read as cream blocks at the lens). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardGlowScale = 0.35f;
	/** Shards fly in their rubble material (dark concrete, critic: never window-coloured confetti); EmberPercent of them
	 *  burn as embers (EmberColor, EmberGlow). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ShardEmberPercent = 18;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ShardEmberGlow = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor ShardEmberColor = FLinearColor(1.f, 0.32f, 0.04f);
	/** Crumb tier (R26, human 2026-11-14 "many small voxel pieces"): every chunk that breaks off mid-fall throws
	 *  CrumbsPerChunk single-voxel crumbs from its volume, every shatter CrumbsPerShatter, every carve CrumbsPerShardCluster
	 *  per shard cluster; edge CrumbSizeCm (x1-CrumbClusterMax voxels), ballistic at CrumbSpeedCms with CrumbBounce
	 *  restitution, lying CrumbLifeSeconds then shrinking over CrumbShrinkSeconds. CrumbMax live at most (oldest recycled).
	 *  Cosmetic, local, instanced: never a Chaos body, never rubble. 0 crumbs = off. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbMax = 1400;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbsPerChunk = 14;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbsPerShatter = 18;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbsPerShardCluster = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbSizeCm = 10.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbClusterMax = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbSpeedCms = 420.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbUpCms = 260.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbBounce = 0.3f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbLifeSeconds = 7.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbShrinkSeconds = 1.2f;
	/** R15 block clear look: rested pieces (over PhysicsFadeSeconds) and crumbs (over CrumbShrinkSeconds) dissolve in a
	 *  world-space noise with a faint warm edge instead of shrinking to a point. Edge colour, glow, width and noise size. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FLinearColor DissolveEdgeColor = FLinearColor(1.f, 0.45f, 0.15f, 1.f);
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DissolveEdgeGlow = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DissolveEdge = 0.12f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DissolveNoiseCm = 18.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbDissolveNoiseCm = 4.f;
	/** R15 (human 2026-11-15): a pile melts unevenly. Each rested piece waits a random 0..DissolveDelayMaxSeconds before
	 *  it starts, and every piece and crumb dissolves over its time x a random 1 +- DissolveDurationJitter. Seeded per
	 *  piece from its rest position (rest poses are replicated: every machine draws the same) and per crumb from the
	 *  crumb field's stream. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DissolveDelayMaxSeconds = 0.9f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DissolveDurationJitter = 0.35f;
	/** R15: a pooled debris block (unit deaths) never shrinks: at the end of its life it flakes into this many dissolving
	 *  crumbs inside its box, after a seeded 0..DissolveDelayMaxSeconds wait of its own. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 DebrisFlakeCrumbs = 6;
	/** Crumbs keep this share of their voxel's glow (lit windows sparkle as they fall, never cream confetti). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbGlowScale = 0.35f;
	/** Crumbs start at most CrumbSpawnPerFrame per frame (the rest queue), and a break throws (1 - CrumbLoadCut x body
	 *  load) of its count, so a busy collapse frame is not where crumbs pile on (advisor a-3). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 CrumbSpawnPerFrame = 120;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float CrumbLoadCut = 0.7f;
	/** R3 faction fire tracers: pool size, bolt length and width (cm), seconds on screen. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 TracerPool = 48;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float TracerLengthCm = 420.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float TracerWidthCm = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float TracerSeconds = 0.07f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float TracerGlow = 6.f;
	/** Collider = fragment bounds times this (adjacent fragments start apart, never nested). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsBoxScale = 0.85f;
	/** Per-body rest deadline = free fall from its release height + SettleSeconds, capped by MaxAirSeconds; a group
	 *  rests when every body is frozen, past its deadline or a stray (hard cap MaxAirSeconds + SettleSeconds). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsMaxAirSeconds = 9.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSettleSeconds = 5.f;
	/** A body farther than this (cm, XY) from its rubble site is a stray: it stops holding its group's rest and, if
	 *  still moving when the group rests, keeps flying while it fades (never set down). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSiteRadiusCm = 1500.f;
	/** Bodies turn solid once their box clears the struck lot's footprint by this margin (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsLotMarginCm = 150.f;
	/** On the street (underside within ContactCm of it) a body switches to these dampings so piles stop jittering. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsGroundContactCm = 120.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsGroundLinearDamping = 1.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsGroundAngularDamping = 3.f;
	/** kg per cm^3 of voxel (mass scales with the fragment's voxels). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsDensity = 0.0012f;
	/** Outward scatter (cm/s) from the piece's centre at the break, plus up kick, and random spin (deg/s). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsScatterCms = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsKickUpCms = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSpinDegS = 120.f;
	/** Fraction of the fall speed the fragments keep at the break (the rest went into the impact). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsKeepFallSpeed = 0.35f;
	/** Horizontal pull (1/s^2) toward the rubble site for SteerSeconds, so the bodies rest over the shared rubble. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSteer = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSteerSeconds = 2.f;
	/** A group rests once every body is asleep or slower than RestCms / RestDegS for RestSeconds. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsRestCms = 40.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsRestDegS = 40.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsRestSeconds = 0.4f;
	/** A body turns solid to the city once its underside is within this height (cm) above the rubble site (fragments
	 *  of a top that broke against its own stump drop clear of it first). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsSolidAboveSiteCm = 120.f;
	/** An impact above this speed change (cm/s) chips the piece: dust and voxel chunks fly off (once per BreakCooldown). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsBreakCms = 500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsBreakCooldown = 0.4f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsLinearDamping = 0.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float PhysicsAngularDamping = 1.0f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float VoxelMaterialGlowScale = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") FDCVoxelCarveDef DefaultCarve;
	/** Decay window: how long the rubble of a fallen piece smokes and burns (s). */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float DecaySeconds = 9.f;
	/** R24 weapon chips (advisor a-37). The host adds each weapon hit's ChipDamage (+Weapons row) to a ChipBucketCm
	 *  bucket of the voxel it hit; past ChipHp (ChipPropHp on props) the hit voxel is chipped (the weapon's ChipRadiusCm)
	 *  and the bucket starts over. Pending chips go out as one Chip op every ChipWindowSeconds while no Chip op is
	 *  out: at most ChipMaxEntries chips on at most ChipMaxCellsPerOp render cells (the rest wait, ChipMaxPending at
	 *  most). A lot shot ChipStructureVoxels voxels since its last structure pass gets one (at most one lot per op,
	 *  ChipStructureMinSeconds apart per lot, only under ChipStructureMaxBodyLoad). Each chip throws ChipCrumbs crumbs. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") bool ChipsEnabled = false;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ChipWindowSeconds = 0.25f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipMaxEntries = 32;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipMaxCellsPerOp = 4;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipMaxPending = 256;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipBucketCm = 30;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ChipHp = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ChipPropHp = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipStructureVoxels = 300;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ChipStructureMinSeconds = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") float ChipStructureMaxBodyLoad = 0.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipCrumbs = 3;
	/** Support (R24): only voxels chipped in a tower's bottom ChipSupportFloors floors count toward ChipStructureVoxels
	 *  (0 = every chipped voxel counts). When the lot is listed, its base gives way on the side the shots came from
	 *  (a bite ChipBiteFloors tall, ChipBitePercent of the footprint deep) before the structure pass, so it topples away
	 *  from the shooters like a strike. Wall-only layers never trip the layer-support rule on their own (a floor slab
	 *  is far bigger), so without the bite no amount of wall fire could bring a tower down. */
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipSupportFloors = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipBiteFloors = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Voxel") int32 ChipBitePercent = 70;
};

/** A prop layer (R2 city props): where one family of voxel-forge props goes. The district's prop rule set turns a
 *  layer on and sets its density with the slots <Id> (weighted models) and <Id>Count (count range per spot: per street
 *  segment, lot, building face or building); Placer picks the placement from the placer registry
 *  (DCDistrictBuilder.cpp): Street (sidewalk, both sides), Lot (scattered in lots of LotKinds), Frontage (in front of
 *  each building face, facing out), Roof (on the roof deck, crafted into the tower grid), Wall (on a facade at a floor
 *  height, crafted into the tower grid), Cable (lines strung across each street segment on two poles). */
USTRUCT(BlueprintType)
struct FDCPropLayer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Placer;
	/** Roof, Wall: the mount from the mount registry (DCVoxelCraft.cpp) the attachments use; None = the placer's own
	 *  (Roof on the deck, Wall on a facade). RoofEdge stands a model at the parapet, facing out over the street. */
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Mount;
	/** Lot kinds the layer uses (Lot, Frontage, Roof, Wall); empty = every lot with a building. */
	UPROPERTY(EditAnywhere, Config, Category = "District") TArray<FName> LotKinds;
	/** Street, Cable: distance in from the kerb (cm). Frontage: gap between the wall and the prop (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "District") float EdgeCm = 180.f;
	/** Spread along a street segment or building face, or the scatter square in a lot (percent of the lot size). */
	UPROPERTY(EditAnywhere, Config, Category = "District") float SpreadPercent = 80.f;
	/** Wall: mount height in floors (0 = street level). Cable: anchor height in cm. */
	UPROPERTY(EditAnywhere, Config, Category = "District") float HeightMin = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "District") float HeightMax = 0.f;
	/** Cable: sag at mid span (cm), line material, a lit bead every BeadEveryCm (0 = none) and the pole material
	 *  (None = no pole). */
	UPROPERTY(EditAnywhere, Config, Category = "District") float SagCm = 80.f;
	UPROPERTY(EditAnywhere, Config, Category = "District") FName Material = TEXT("Ink");
	UPROPERTY(EditAnywhere, Config, Category = "District") FName BeadMaterial;
	UPROPERTY(EditAnywhere, Config, Category = "District") float BeadEveryCm = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "District") FName PoleMaterial;
	/** R24: a prop of this layer breaks apart once weapon chips took this percent of its voxels (0 = never breaks). */
	UPROPERTY(EditAnywhere, Config, Category = "District") int32 ChipBreakPercent = 35;
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
	/** R23 district variants: the sector this row is rolled for (-1 = picked by row index, one row per sector) and its
	 *  weight among the rows of that sector (seeded per sector: every machine builds the same district). */
	UPROPERTY(EditAnywhere, Config, Category = "District") int32 Sector = -1;
	UPROPERTY(EditAnywhere, Config, Category = "District") float Weight = 1.f;
	/** R23: lot kinds that hold no building and are paved like a plaza (markets, yards, courts, parking), dressed by Lot
	 *  prop layers that name them. */
	UPROPERTY(EditAnywhere, Config, Category = "District") TArray<FName> OpenKinds;
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
struct FDCVoxelRest
{
	GENERATED_BODY()
	/** Fragment index in the piece's fragment tree (DCVoxelOps::FragmentTree order). */
	UPROPERTY() int32 Frag = 0;
	/** Where the host's body came to rest: its centre in sector cm, and its orientation snapped to the nearest axis
	 *  frame (AxisX/AxisZ: 0..5 = +X -X +Y -Y +Z -Z, where the fragment's local X and Z axes ended up). */
	UPROPERTY() FIntVector PosCm = FIntVector::ZeroValue;
	UPROPERTY() uint8 AxisX = 0;
	UPROPERTY() uint8 AxisZ = 4;
};

/** R24 one weapon chip (advisor a-37): every solid voxel of one grid within RadiusCm of CenterCm (sector cm, the hit
 *  voxel's centre, quantized on the host) is cleared. Grid: a lot's tower grid (0..N-1) or DCVoxelOps::PropGrid. */
USTRUCT()
struct FDCVoxelChip
{
	GENERATED_BODY()
	UPROPERTY() int32 Grid = 0;
	UPROPERTY() FIntVector CenterCm = FIntVector::ZeroValue;
	UPROPERTY() int32 RadiusCm = 0;
};

USTRUCT()
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
	/** Kind "Rest" (R26, host-owned rubble): the host's physics bodies of piece RestPiece of op RestOf came to rest
	 *  here; every machine replaces that piece's provisional rubble with the fragments settled at these poses. */
	UPROPERTY() int32 RestOf = 0;
	UPROPERTY() int32 RestPiece = -1;
	UPROPERTY() TArray<FDCVoxelRest> Rests;
	/** Kind "Chip" (R24, host-batched weapon hits): the spheres to clear, in order; StructureLots get the structure
	 *  pass after them (the host lists a lot once enough of it was shot away, CenterCm = where the chips came from). */
	UPROPERTY() TArray<FDCVoxelChip> Chips;
	UPROPERTY() TArray<int32> StructureLots;
	/** Props shot past their break fraction: each entry clears a column of the prop grid (CenterCm X/Y = the prop's
	 *  foot, CenterCm Z = its top, RadiusCm = its reach) and the prop flies apart as pieces and crumbs. */
	UPROPERTY() TArray<FDCVoxelChip> BreakProps;
};
