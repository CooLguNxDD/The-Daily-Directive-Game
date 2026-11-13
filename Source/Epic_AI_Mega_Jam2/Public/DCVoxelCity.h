// The voxel city on one machine (R1-R3): crafts the sector's grids from the run seed, applies the host's replicated
// voxel ops in Seq order, meshes dirty chunks on worker threads and uploads them time-sliced into one procedural mesh
// per render cell, and plays the cosmetic fall of broken pieces. Local only, never replicated: every machine builds
// the same voxels from the same seed and ops.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelMesher.h"
#include "Voxel/DCVoxelOps.h"
#include "DCVoxelCity.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCVoxelCity : public AActor
{
	GENERATED_BODY()

public:
	ADCVoxelCity();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The sector plan every machine agrees on: lots from the seed, recipes applied when the voxel city is on. */
	static FDCCityPlan PlanSector(int32 RunSeed, int32 Sector);
	static bool IsEnabled();
	static ADCVoxelCity* Get(const UObject* WorldContext);

	/** Crafts the sector (clears the old one). BuildId tags which ops belong to this build. */
	void Build(int32 RunSeed, int32 Sector, int32 BuildId);
	void Clear();

	/** Applies one op now (host: right after appending it; reel: at its replay time). Animate plays the fall. */
	FDCVoxelOpResult ApplyOp(const FDCVoxelOp& Op, bool bAnimate);

	/** Condemned-tower shake: offsets every mesh of a lot (cosmetic). */
	void SetTowerOffset(int32 Lot, const FVector& Offset);

	/** Follows the game state (sector builds and op list) when on; the reel turns it off and directs it itself. */
	void SetFollowGameState(bool bFollow) { bFollowGameState = bFollow; }

	int32 GetBuiltSector() const { return BuiltSector; }
	int32 GetBuildId() const { return BuiltId; }
	uint32 GetHash() const { return Sector.Hash(); }
	const FDCVoxelSector& GetSector() const { return Sector; }
	bool IsMeshing() const { return Jobs.Num() > 0 || DirtyCells.Num() > 0; }

private:
	struct FMeshJob;
	struct FFalling;

	void FollowGameState();
	void QueueDirty(int32 GridIndex, const TSet<FIntVector>& Chunks);
	void QueueAllDirty(TSet<FIntVector>* DeferTerrain);
	void PollJobs();
	void UploadCells();
	void TickFalling(float Now);
	void StartFall(const FDCVoxelPiece& Piece, float Now, const TArray<FIntVector>& DeferredTerrain);
	void Land(FFalling& F);

	FDCVoxelGrid& GridAt(int32 GridIndex);
	const FDCVoxelGrid& GridAt(int32 GridIndex) const;
	int32 NumGrids() const { return Sector.Towers.Num() + 1; }
	FIntVector CellOf(int32 GridIndex, const FIntVector& Chunk) const;
	UProceduralMeshComponent* NewMesh(bool bCollision);
	UMaterialInterface* VoxelMaterial();

	FDCVoxelSector Sector;
	FDCVoxelOpContext OpCtx;
	TArray<FColor> Colors;
	int32 BuiltSector = INDEX_NONE;
	int32 BuiltId = 0;
	int32 BuiltSeed = 0;
	int32 AppliedSeq = 0;
	bool bFollowGameState = true;

	/** Grid index 0 = terrain, 1 + lot = towers. Key = (grid, chunk xyz). */
	TMap<FIntVector4, FDCVoxelMeshData> ChunkMeshes;
	TMap<FIntVector4, TSet<FIntVector>> CellChunks;
	TMap<FIntVector4, TObjectPtr<UProceduralMeshComponent>> Cells;
	TSet<FIntVector4> DirtyCells;
	TArray<TSharedPtr<FMeshJob>> Jobs;
	TArray<TSharedPtr<FFalling>> Falling;
	/** Terrain chunks whose rubble shows when a falling piece lands (absolute time). */
	TArray<TPair<float, FIntVector>> DeferredTerrain;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> MeshPool;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
};
