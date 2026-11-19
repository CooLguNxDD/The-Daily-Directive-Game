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

/** A tower voxel the strike blew out (R26 shatter): where it was and what it was. Cosmetic: the city turns these
 *  into flying shard bodies; never part of the hash or the rubble. */
struct EPIC_AI_MEGA_JAM2_API FDCVoxelShard
{
	int32 Lot = INDEX_NONE;
	FIntVector Cell = FIntVector::ZeroValue;
	uint8 Material = 0;
};

/** A physics piece kept after its op (R26): its cells, its provisional rubble, and its fragment tree, so a later
 *  Rest op can replace the rubble with the fragments settled where the host's bodies came to rest. */
struct EPIC_AI_MEGA_JAM2_API FDCPendingPiece
{
	int32 Lot = INDEX_NONE;
	TArray<FIntVector> Cells;
	TArray<uint8> Materials;
	TArray<FIntVector> Rubble;
	TArray<uint8> RubbleMaterials;
	/** Fragment tree: chunk, its leaves, next chunk... Parent = INDEX_NONE for a chunk; cells index Cells. */
	TArray<int32> FragParent;
	TArray<TArray<int32>> FragCells;
	/** Per fragment: min and max cell (inclusive), derived on the worker so the game thread never scans cells. */
	TArray<FIntVector> FragLo;
	TArray<FIntVector> FragHi;
};

/** A cluster of carved voxels that flies as one shard body (R26), derived off the game thread. */
struct EPIC_AI_MEGA_JAM2_API FDCShardCluster
{
	FVector Pos = FVector::ZeroVector;
	int32 Count = 0;
	uint8 Material = 0;
	int32 Block = 1;
	float VoxelCm = 10.f;
	/** R24: a broken prop's piece (a pooled debris block), not grit. */
	bool bPiece = false;
};

struct EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult
{
	/** Tower voxels the carve and the bite removed (shatter source, R26). */
	TArray<FDCVoxelShard> Shards;
	/** Cosmetic data derived on the op's worker (DeriveCosmetics): shard clusters to fly, and the physics pieces
	 *  (piece index -> cells, provisional rubble, fragment tree) kept for the host's Rest op. Never hashed. */
	TArray<FDCShardCluster> ShardClusters;
	TArray<TPair<int32, FDCPendingPiece>> Pending;
	/** Lots whose tower broke at a layer and toppled (the host turns these into collapse records). */
	TArray<int32> FallenLots;
	/** Lots whose grid changed (remesh). */
	TArray<int32> TouchedLots;
	TArray<FDCVoxelPiece> Pieces;
	/** Terrain cells the pieces settled into (already written to the terrain grid by Apply). */
	TArray<FIntVector> Rubble;
	TArray<uint8> RubbleMaterials;
	int32 Carved = 0;
	/** R24 Chip ops: voxels each chip removed from a tower, as (lot, voxels) in chip order (the host's structure
	 *  trigger counts them). Never hashed. */
	TArray<FIntPoint> ChipCarved;
	/** R24 Chip ops: voxels each entry of Op.Chips removed, in order (the host credits props with them). */
	TArray<int32> ChipVoxels;
	/** Where the op's time went (ms): carve and bite, structure pass, settle (evidence for the impact hitch, R13). */
	float CarveMs = 0.f;
	float StructureMs = 0.f;
	float SettleMs = 0.f;
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
	/** Street props, plaza dressing and wrecks from voxel-forge models (R16, R17), 5 cm. */
	FDCVoxelGrid Props;
	/** Content hash over every grid (determinism evidence: host and clients compare after each op). */
	uint32 Hash() const;
};

/** One op kind (rule 3: dispatch by id, advisor a-37): how it applies, and what the city must do around it. */
struct EPIC_AI_MEGA_JAM2_API FDCOpKind
{
	FDCVoxelOpResult (*Apply)(FDCVoxelSector&, const FDCVoxelOp&, const FDCVoxelOpContext&, const FDCPendingPiece&) = nullptr;
	/** The op can change the street (crater, rubble): the city snapshots street tops before it and drops its surface
	 *  cache after it. A Chip op writes the street only through its StructureLots. */
	bool bWritesTerrain = true;
	/** Carved tower voxels fly as physics shard bodies (else the op's clusters throw crumbs only). */
	bool bShardBodies = true;
};

namespace DCVoxelOps
{
	/** FDCVoxelChip::Grid of the prop grid (towers are 0..N-1). */
	constexpr int32 PropGrid = -1;
	/** No chip clears a sphere wider than this (validated on every machine). */
	constexpr int32 ChipMaxRadiusCm = 200;

	/** The kind registry: Strike, Fell, Rest, Chip. Null for an unknown kind. */
	EPIC_AI_MEGA_JAM2_API const FDCOpKind* FindKind(FName Kind);
	/** Applies any op through its kind's row (Pend: the pending piece a Rest op settles). Unknown kinds do nothing. */
	EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult Run(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, const FDCPendingPiece& Pend);
	/** True when the op may change the terrain grid (street tops). */
	EPIC_AI_MEGA_JAM2_API bool WritesTerrain(const FDCVoxelOp& Op);
	/** A Chip op without a structure pass: only spheres cleared, so consecutive ones fold into one task. */
	EPIC_AI_MEGA_JAM2_API bool IsChipOnly(const FDCVoxelOp& Op);

	/** Removes every solid voxel of a grid within RadiusCm of CenterCm (voxel centres, integer). Returns how many. */
	/** The highest solid tower voxel under terrain column (X, Y), as a terrain cell index (0 = none): rubble rests on
	 *  what is left of a tower, not on its uncarved roof prism. */
	/** Photo sight through the sector: true when voxels block From -> To (towers in IgnoreLots never do; a standing
	 *  shell blocks through its hollow inside). Marches StepCm; the first SkipNearCm and last SkipFarCm never block. */
	EPIC_AI_MEGA_JAM2_API bool SightBlocked(const struct FDCVoxelSector& Sector, const FVector& From, const FVector& To, const TArray<int32>& IgnoreLots, float StepCm, float SkipNearCm, float SkipFarCm);
	EPIC_AI_MEGA_JAM2_API int32 TowerTopCells(const struct FDCVoxelTower& T, const FDCVoxelGrid& G, int32 X, int32 Y);
	/** MinZ: local layers below it are never carved (the terrain's bedrock). */
	EPIC_AI_MEGA_JAM2_API int32 CarveSphere(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 MinZ = 0, TArray<TPair<FIntVector, uint8>>* Removed = nullptr);
	/** Removes a vertical cylinder from the ground up to the grid's top. */
	EPIC_AI_MEGA_JAM2_API int32 CarveColumn(FDCVoxelGrid& Grid, const FIntVector& CenterCm, int32 RadiusCm, int32 TopZ, TArray<TPair<FIntVector, uint8>>* Removed = nullptr);
	/** Bites the base of a tower on the side facing FromCm: BiteFloors tall, BitePercent of the footprint deep, ragged. */
	EPIC_AI_MEGA_JAM2_API int32 Bite(FDCVoxelTower& Tower, const FIntVector& FromCm, int32 BiteFloors, int32 BitePercent, int32 BayCm, TArray<TPair<FIntVector, uint8>>* Removed = nullptr);

	/** Structure pass on one tower: the highest layer under SupportPercent of its intact count breaks and everything
	 *  above it topples along Dir; then any island not connected to the ground drops. Removes pieces from the grid. */
	EPIC_AI_MEGA_JAM2_API void Structure(FDCVoxelTower& Tower, const FIntVector& Dir, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out);

	/** Settles a piece into the terrain grid: downsampled to terrain cells in first-seen order, drifted by height,
	 *  slid to the angle of repose, stacked on rubble and stumps, never into a standing tower. Writes the terrain. */
	EPIC_AI_MEGA_JAM2_API void Settle(FDCVoxelSector& Sector, FDCVoxelPiece& Piece, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Out);

	/** One replicated op (through Run). Strike: beam column + blast sphere in every tower it reaches, a crater in the
	 *  terrain, a bite in every standing tower within the blast radius, then the structure pass and settle. Fell: bite
	 *  one lot. Chip: the listed spheres, then the structure pass on StructureLots only. */
	EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult Apply(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx);

	/** Deterministic per-voxel hash (no RNG state, order independent). */
	EPIC_AI_MEGA_JAM2_API uint32 Hash(const FIntVector& P, uint32 Salt);

	/** Breaks a falling piece into fragments for the physics bodies (R26): its cells bucketed on a block grid of
	 *  BlockCells voxels, the block doubled until there are at most MaxFragments. Returns cell indices per fragment,
	 *  ordered by block coordinate (the same on every machine; the bodies themselves are local and cosmetic). */
	EPIC_AI_MEGA_JAM2_API TArray<TArray<int32>> SplitFragments(const TArray<FIntVector>& Cells, int32 BlockCells, int32 MaxFragments, const FIntVector* Focus = nullptr, float FalloffCells = 0.f);
	/** The piece's fragment tree (chunks of ChunkCm blocks, each split into leaves of LeafCm blocks), in the order
	 *  the city builds its bodies: chunk, its leaves, next chunk. Pure, the same on every machine. */
	EPIC_AI_MEGA_JAM2_API void FragmentTree(const TArray<FIntVector>& Cells, int32 VoxelCm, float ChunkCm, int32 MaxChunks, float LeafCm, int32 MaxLeaves,
		TArray<int32>& OutParent, TArray<TArray<int32>>& OutCells, const FIntVector* Focus = nullptr, float FocusFalloffCm = 0.f);
	/** Axis frame index (0..5 = +X -X +Y -Y +Z -Z) of the world axis nearest Dir, and back. */
	EPIC_AI_MEGA_JAM2_API uint8 AxisIndex(const FVector& Dir);
	EPIC_AI_MEGA_JAM2_API FIntVector AxisDir(uint8 Index);
	/** Rest op (R26): removes the piece's provisional rubble and settles each rested fragment's cells, turned to its
	 *  snapped orientation and moved to the host's rest centre, into the terrain (Settle: stacks, never floats). */
	/** Off the game thread, after Apply: clusters and samples the shards (ShardMax, weighted to the blast centre) and
	 *  builds the fragment trees of the PhysicsPieces biggest pieces; frees the raw shard list. */
	EPIC_AI_MEGA_JAM2_API void DeriveCosmetics(const FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCVoxelOpContext& Ctx, FDCVoxelOpResult& Result);
	EPIC_AI_MEGA_JAM2_API FDCVoxelOpResult ApplyRest(FDCVoxelSector& Sector, const FDCVoxelOp& Op, const FDCPendingPiece& Piece, const FDCVoxelOpContext& Ctx);
}
