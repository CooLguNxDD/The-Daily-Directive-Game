// The voxel city on one machine (R1-R3): crafts the sector's grids from the run seed, applies the host's replicated
// voxel ops in Seq order, meshes dirty chunks on worker threads and uploads them time-sliced into one procedural mesh
// per render cell, and plays the cosmetic fall of broken pieces. Local only, never replicated: every machine builds
// the same voxels from the same seed and ops.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tasks/Task.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelMesher.h"
#include "Voxel/DCVoxelOps.h"
#include "Rules/DCWeather.h"
#include "DCDebrisPool.h"
#include "DCVoxelCity.generated.h"

class UBoxComponent;

class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCVoxelCity : public AActor
{
	GENERATED_BODY()

public:
	ADCVoxelCity();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The sector plan every machine agrees on: lots from the seed, recipes applied when the voxel city is on. */
	static FDCCityPlan PlanSector(int32 RunSeed, int32 Sector);
	static bool IsEnabled();
	static ADCVoxelCity* Get(const UObject* WorldContext);

	/** Crafts the sector (clears the old one). BuildId tags which ops belong to this build. bAsync crafts on a worker
	 *  (the sector card covers it; R13: the craft was a 0.7-1 s game-thread frame) and goes live in Tick when done;
	 *  until then the city reads as not built (no ops apply). The reel builds synchronously. */
	void Build(int32 RunSeed, int32 Sector, int32 BuildId, bool bAsync = false);
	bool IsCrafting() const { return PendingCraft.IsValid(); }
	void Clear();

	/** Applies one op now (host: right after appending it; reel: at its replay time). Animate plays the fall. */
	FDCVoxelOpResult ApplyOp(const FDCVoxelOp& Op, bool bAnimate);
	/** Queues an op for the worker (play): ops run one at a time in Seq order; OnDone gets the result and the voxel hash
	 *  on the game thread when it lands. */
	void ApplyOpAsync(const FDCVoxelOp& Op, bool bAnimate, TFunction<void(const FDCVoxelOpResult&, uint32)> OnDone);

	/** Condemned-tower shake: offsets every mesh of a lot (cosmetic). */
	void SetTowerOffset(int32 Lot, const FVector& Offset);

	/** Follows the game state (sector builds and op list) when on; the reel turns it off and directs it itself. */
	void SetFollowGameState(bool bFollow) { bFollowGameState = bFollow; }

	int32 GetBuiltSector() const { return BuiltSector; }
	/** True once a build's cells are all up (the sector title card holds until then). */
	bool IsLoaded() const { return BuiltSector != INDEX_NONE && !bLoading; }
	class ADCVoxelFx* GetFx() const { return Fx; }
	/** Surface wetness 0..1 from the weather (R18): every voxel material darkens and glosses its up faces. */
	void SetWetness(float Wetness);

	/** Top of the street, rubble included, at a point (cm): what walkers stand on and players are lifted onto. */
	float SurfaceZCm(const FVector& Where) const;
	/** Is this world point inside a solid voxel (towers, terrain, props)? Photos never shoot from inside one. */
	bool IsSolidAt(const FVector& Where) const;
	/** Photo sight (host grading): is the straight line From -> To blocked by city voxels? Marches the voxel city,
	 *  never physics (async-cooked collision lags the shown world); lots in IgnoreLots never block (a strike's own
	 *  tower is its subject). Unknown while an op writes on a worker: not blocked. */
	bool IsSightBlocked(const FVector& From, const FVector& To, const TArray<int32>& IgnoreLots) const;
	/** Has this tower fallen in the shown (committed) world? Game-thread state, never the worker's plan. */
	bool IsTowerFallen(int32 Lot) const { return CommittedFallen.IsValidIndex(Lot) && CommittedFallen[Lot]; }
	/** Towers whose footprint lies within RadiusCm of a point (what a blast there strikes). */
	void LotsNear(const FVector& Where, float RadiusCm, TArray<int32>& Out) const;

	/** The sector's season (rolled from the run seed by the district's weather rules) and its arc (R18). */
	static const struct FDCSeasonDef* SeasonFor(int32 RunSeed, int32 Sector);
	static struct FDCWeatherState WeatherAt(int32 RunSeed, int32 Sector, float Fraction);
	int32 GetBuildId() const { return BuiltId; }
	/** Highest op Seq applied or queued on this city. */
	int32 GetAppliedSeq() const { return AppliedSeq; }
	uint32 GetHash() const { return Sector.Hash(); }
	const FDCVoxelSector& GetSector() const { return Sector; }
	bool IsMeshing() const { return Jobs.Num() > 0 || DirtyCells.Num() > 0; }
	/** Tower pieces falling right now (an active collapse, for the perf probe). */
	int32 GetFallingCount() const { return Falling.Num(); }
	/** An op due by Time (its apply time) is still planning, held or queued: its results have not landed yet. */
	bool HasPendingOpsAt(float Time) const;

private:
	struct FMeshJob;
	struct FFalling;

	void FollowGameState();
	/** Builds or clears to match the game state, without applying ops. */
	void SyncBuild();
	void QueueDirty(int32 GridIndex, const TSet<FIntVector>& Chunks);
	void QueueAllDirty(TSet<FIntVector>* DeferTerrain);
	void PollJobs();
	void UploadCells();
	void TickFalling(float Now);
	void StartFall(const FDCVoxelPiece& Piece, float Now, const TArray<FIntVector>& DeferredTerrain, const FVector& LandAt, bool bPhysics = false, int32 OpSeq = 0, int32 PieceIndex = INDEX_NONE);
	/** R26 host-owned rubble: physics pieces kept per op Seq and piece index until their Rest op lands. */
	TMap<int32, TMap<int32, FDCPendingPiece>> PendingPieces;
	FDCPendingPiece PendingFor(const FDCVoxelOp& Op) const;
	/** Host: its bodies of a piece came to rest; append and apply the Rest op (poses snapped to cm and axis frames). */
	void HostRest(FFalling& F);
public:
	/** Off for cities that never append ops (the reel's re-staged city replays the recorded Rest ops instead). */
	bool bHostRests = true;
private:
	/** R26: a fragmented piece hits the street: each fragment becomes a Chaos body carrying its mesh. */
	void BreakApart(const TSharedPtr<FFalling>& F, float Age, float U, bool bImpact);
	/** One fragment (chunk or leaf) becomes a simulating box body carrying its mesh. */
	class UBoxComponent* MakeBody(const TSharedPtr<FFalling>& F, int32 Index, const FVector& At, const FQuat& Rot, const FVector& V, const FVector& SpinDeg, bool bSolid);
	/** Pooled body boxes (advisor CP10): prewarmed to PhysicsMaxBodies at build, parked with collision off. */
	UPROPERTY() TArray<TObjectPtr<UBoxComponent>> BoxAll;
	UPROPERTY() TArray<TObjectPtr<UBoxComponent>> BoxFree;
	class UBoxComponent* AcquireBox();
	void ReleaseBox(UPrimitiveComponent* Body);
	void PrewarmBodies();
	/** A chunk that took a hard hit breaks into its leaves (each a body). */
	void Shatter(const TSharedPtr<FFalling>& F, int32 ChunkIndex);
	/** Steers, solidifies and shatters the bodies; true when they rest (all still or out of air time). */
	bool TickBodies(const TSharedPtr<FFalling>& F, float Dt);
	/** R26 shatter: carved tower voxels as flying shard bodies, spawned at the op's presentation start. */
	void QueueShards(const FDCVoxelOp& Op, const FDCVoxelOpResult& Result, float Start);
	void SpawnDueShards(float Now);
	struct FDCShardBurst
	{
		float Start = 0.f;
		TArray<FDCDebrisSpawn> Spawns;
	};
	TArray<FDCShardBurst> PendingShards;
	struct FDCFading
	{
		TWeakObjectPtr<UPrimitiveComponent> Body;
		TWeakObjectPtr<UPrimitiveComponent> Mesh;
		float Start = 0.f;
	};
	TArray<FDCFading> Fading;
	/** -dccollapsecam evidence camera (step -1 = off). */
	void TickCollapseCam();
	int32 CollapseCamStep = -1;
	int32 CollapseCamOp = 0;
	FVector CollapseCamCenter = FVector::ZeroVector;
	float CollapseCamStart = 0.f;
	/** Street height under a body (terrain top, capped at the rubble site: the op's own rubble is not shown yet). */
	float BodyGroundZ(const FVector& At, const FVector& LandCenter) const;
	/** Lowest fragment's underside above the street (<= 0: the falling piece hit the ground). */
	float LowestFragmentAboveGround(const FFalling& F) const;
	/** Live and planned bodies (the PhysicsMaxBodies budget). */
	int32 BodiesReserved = 0;
	UFUNCTION() void OnBodyHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	/** Pieces with a live body, by their collider (for hits). */
	TMap<TObjectKey<UPrimitiveComponent>, TWeakPtr<FFalling>> Bodies;
	void Land(FFalling& F);
	void TickCityShot();
	void StartNextOp();
	void WaitInFlight();
	void FinishInFlight();
	void FinishOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float ApplyMs, uint32 Hash, bool bOffThread);
	/** Two-phase ops, visual half: meshes the op's chunks and pieces while it is held (uploads wait), falls start at
	 *  StartWorldTime. CommitOp is the rest at the apply time (uploads released, fx, player lift, log). */
	void PrepareOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float StartWorldTime);
	void CommitOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float ApplyMs, uint32 Hash, bool bOffThread);
	/** Set while a prepared op waits for its apply time: that op's cells (HeldCells) hold so the old mesh shows until
	 *  the commit; every other cell keeps uploading under the budget. */
	bool bHoldUploads = false;
	TSet<FIntVector4> HeldCells;
	/** The commit frame uploads exactly the op's cells at once (crater and broken towers in the detonation frame); cells
	 *  whose mesh job is still running go first on the next frames. Never a lifted budget for the rest. */
	TSet<FIntVector4> CommitCells;
	bool UploadCell(const FIntVector4& Cell);
	/** Set by Build until the first full upload is done (load-phase budget). */
	bool bLoading = false;
	/** An off-thread sector craft in flight (Build with bAsync); FinishBuild swaps it in. */
	/** A chunk's padded voxel snapshot taken on the craft worker (the mesh job input), so the game thread never copies
	 *  every chunk of a fresh city in one frame (R13: ~0.7 s sector-start frame). */
	struct FPrePad
	{
		int32 Grid = 0;
		FIntVector Chunk = FIntVector::ZeroValue;
		TArray<uint8> Padded;
	};
	struct FPendingCraft
	{
		TSharedPtr<FDCVoxelSector> Out;
		TSharedPtr<TArray<FPrePad>> Pads;
		TSharedPtr<uint32> HashOut;
		UE::Tasks::FTask Task;
		int32 RunSeed = 0;
		int32 SectorIndex = INDEX_NONE;
		int32 BuildId = 0;
		double Start = 0.0;
	};
	TSharedPtr<FPendingCraft> PendingCraft;
	void FinishBuild(int32 RunSeed, int32 SectorIndex, int32 BuildId, double Start, TArray<FPrePad>* Pads = nullptr, uint32 Hash = 0);
	void QueuePadded(TArray<FPrePad>& Pads);
	/** Waits for an in-flight craft and swaps it in (an op never applies to a half-built city). */
	void FlushPendingCraft();
	struct FQueuedOp
	{
		FDCVoxelOp Op;
		bool bAnimate = true;
		TFunction<void(const FDCVoxelOpResult&, uint32)> OnDone;
		FDCVoxelOpResult Result;
		uint32 Hash = 0;
		float ApplyMs = 0.f;
		float DeriveMs = 0.f;
		float HashMs = 0.f;
		bool bPrepared = false;
	};
	TArray<FQueuedOp> QueuedOps;
	TSharedPtr<FQueuedOp> InFlight;
	UE::Tasks::FTask InFlightTask;
	mutable TMap<FIntPoint, float> SurfaceCache;
	/** While an op plans or waits for its apply time, street tops inside this circle (cm) come from a snapshot taken
	 *  before it started (the sector already holds the future there); outside it the sector is unchanged. */
	FVector2D HoldCenterCm = FVector2D::ZeroVector;
	float HoldRadiusCm = -1.f;
	TMap<FIntPoint, float> HoldTops;
	TArray<bool> CommittedFallen;
	float ScanSurface(const FIntVector& Column) const;
	int32 ShotStep = 0;
	double NextShotReal = 0.0;
	UPROPERTY() TObjectPtr<class ACameraActor> ShotCamera;

	FDCVoxelGrid& GridAt(int32 GridIndex);
	const FDCVoxelGrid& GridAt(int32 GridIndex) const;
	int32 NumGrids() const { return Sector.Towers.Num() + 2; }
	FIntVector CellOf(int32 GridIndex, const FIntVector& Chunk) const;
	UProceduralMeshComponent* NewMesh(bool bCollision, int32 VoxelCm);
	/** Falling-piece meshes come from a pool keyed by voxel size (advisor CP17, R13 collapse frames): released, never
	 *  destroyed, during play; reset fully on release (sections, attachment, transform, visibility). */
	UProceduralMeshComponent* AcquirePiece(int32 VoxelCm);
	UProceduralMeshComponent* NewPiece(int32 VoxelCm);
	void ReleasePiece(UPrimitiveComponent* Piece);
	void PrewarmPieces(int32 PerFrame);
	TMap<int32, TArray<TObjectPtr<UProceduralMeshComponent>>> FreePieces;
	TMap<TObjectPtr<UProceduralMeshComponent>, int32> PieceCm;
	/** Every pooled piece, so garbage collection sees them (the maps above are not UPROPERTYs). */
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> PieceAll;
	UMaterialInterface* VoxelMaterial();
	/** One instance of the voxel material per voxel size: its edge bevel sits on that grid (V4.2). */
	UMaterialInterface* VoxelMaterialFor(int32 VoxelCm);
	UPROPERTY() TMap<int32, TObjectPtr<class UMaterialInstanceDynamic>> VoxelMIDs;
	float Wetness = 0.f;
	/** Seconds this machine's presentation clock has fallen behind world time (falls and rubble reveals use it). */
	float FallOffset = 0.f;

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
	/** Pooled sign lights (R25) and the signs whose voxels were carved (lot, sign index): their light is gone. */
	UPROPERTY() TArray<TObjectPtr<class URectLightComponent>> SignLightPool;
	TSet<FIntPoint> DeadSigns;
	/** Aftermath firelights over fresh rubble piles (pooled, FireLights at most). */
	UPROPERTY() TArray<TObjectPtr<class UPointLightComponent>> FireLightPool;
	TArray<float> FireLightStart;
	/** Street lamp spots (from the plan's props) and their pooled lights; a spot is cleared when a blast reaches it. */
	TArray<FVector> LampSpots;
	UPROPERTY() TArray<TObjectPtr<class UPointLightComponent>> LampPool;
	float NextLampPick = 0.f;
	void TickLampLights();
	void StartFireLight(const FVector& Where);
	void TickFireLights();
	float NextSignPick = 0.f;
	void TickSignLights();
	/** Game thread, after a commit: signs on touched lots whose centre voxel is gone. */
	void KillCarvedSigns(const TArray<int32>& TouchedLots);
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
	UPROPERTY() TObjectPtr<class ADCVoxelFx> Fx;
};
