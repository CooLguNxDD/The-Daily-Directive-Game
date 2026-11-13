#include "DCVoxelCity.h"

#include "DCVoxelFx.h"

#include "DCAssets.h"
#include "DCAudio.h"
#include "DCDebrisPool.h"
#include "DCEnvironment.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Tasks/Task.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelSettings.h"

struct ADCVoxelCity::FMeshJob
{
	int32 Grid = 0;
	FIntVector Chunk;
	/** Built from the grid when the job was queued; the result belongs to that build only. */
	int32 BuildId = 0;
	FDCVoxelMeshData Mesh;
	UE::Tasks::FTask Task;
	/** A falling piece's mesh (no chunk). */
	TSharedPtr<FFalling> Piece;
};

struct ADCVoxelCity::FFalling
{
	TObjectPtr<UProceduralMeshComponent> Comp;
	FVector Pivot;
	FVector Axis;
	float Start = 0.f;
	float Duration = 1.f;
	float MaxAngle = 0.f;
	float DropCm = 0.f;
	FVector LandCenter;
	FLinearColor Color;
	bool bReady = false;
	bool bLanded = false;
};

namespace
{
	const FName CityTag(TEXT("DCVoxelCity"));

	FIntVector4 Key(int32 Grid, const FIntVector& V) { return FIntVector4(Grid, V.X, V.Y, V.Z); }
	FIntVector Xyz(const FIntVector4& K) { return FIntVector(K.Y, K.Z, K.W); }
}

ADCVoxelCity::ADCVoxelCity()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetReplicates(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Tags.Add(CityTag);
}

bool ADCVoxelCity::IsEnabled()
{
	return UDCVoxelSettings::Get().IsVoxelCity();
}

ADCVoxelCity* ADCVoxelCity::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
	return GS ? GS->GetVoxelCity() : nullptr;
}

FDCCityPlan ADCVoxelCity::PlanSector(int32 RunSeed, int32 SectorIndex)
{
	const UDCTuning& T = UDCTuning::Get();
	FDCCityPlan Plan = DCCityRules::PlanCity(DCCityRules::SectorSeed(RunSeed, SectorIndex), T.City, T.Look.BuildingColors.Num());
	if (IsEnabled())
	{
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		DCVoxelCraft::ApplyRecipes(Plan, V.SectorLayout(SectorIndex), V.Recipes, FMath::FloorToInt(T.City.LotSizeCm / T.City.BlockSizeCm));
	}
	return Plan;
}

UMaterialInterface* ADCVoxelCity::VoxelMaterial()
{
	if (!Material)
	{
		Material = Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad());
		if (!Material)
		{
			Material = DCAssets::FlatMaterial();
		}
	}
	return Material;
}

void ADCVoxelCity::BeginPlay()
{
	Super::BeginPlay();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Fx = GetWorld()->SpawnActor<ADCVoxelFx>(ADCVoxelFx::StaticClass(), FTransform::Identity, Params);
}

void ADCVoxelCity::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const TSharedPtr<FMeshJob>& Job : Jobs)
	{
		Job->Task.Wait(); // jobs only touch their own data, but must not outlive the actor
	}
	Jobs.Reset();
	Super::EndPlay(Reason);
}

FDCVoxelGrid& ADCVoxelCity::GridAt(int32 GridIndex)
{
	return GridIndex == 0 ? Sector.Terrain : Sector.Towers[GridIndex - 1].Grid;
}

const FDCVoxelGrid& ADCVoxelCity::GridAt(int32 GridIndex) const
{
	return GridIndex == 0 ? Sector.Terrain : Sector.Towers[GridIndex - 1].Grid;
}

FIntVector ADCVoxelCity::CellOf(int32 GridIndex, const FIntVector& Chunk) const
{
	// Render cells are sized in cm (never smaller than one chunk), so the component count stays flat across voxel sizes.
	const int32 ChunkCm = FDCVoxelChunk::Size * GridAt(GridIndex).VoxelCm;
	const int32 PerCell = FMath::Max(1, UDCVoxelSettings::Get().Tuning.RenderCellCm / ChunkCm);
	return DCVoxelMath::FloorDiv(Chunk, PerCell);
}

UProceduralMeshComponent* ADCVoxelCity::NewMesh(bool bCollision)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->bUseAsyncCooking = true;
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
	Mesh->SetCastShadow(true);
	Mesh->RegisterComponent();
	Mesh->SetMaterial(0, VoxelMaterial());
	MeshPool.Add(Mesh);
	return Mesh;
}

void ADCVoxelCity::Clear()
{
	for (const TSharedPtr<FMeshJob>& Job : Jobs)
	{
		Job->Task.Wait();
	}
	Jobs.Reset();
	for (UProceduralMeshComponent* Mesh : MeshPool)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	MeshPool.Reset();
	Cells.Reset();
	CellChunks.Reset();
	ChunkMeshes.Reset();
	DirtyCells.Reset();
	Falling.Reset();
	DeferredTerrain.Reset();
	if (Fx)
	{
		Fx->ClearAll();
	}
	Sector.Towers.Reset();
	Sector.Terrain.Data.Reset();
	BuiltSector = INDEX_NONE;
	AppliedSeq = 0;
}

void ADCVoxelCity::Build(int32 RunSeed, int32 SectorIndex, int32 BuildId)
{
	Clear();
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const UDCTuning& T = UDCTuning::Get();
	const double Start = FPlatformTime::Seconds();
	const FDCCityPlan Plan = PlanSector(RunSeed, SectorIndex);
	const FDCVoxelCraftContext Craft = V.CraftContext(T.City.BlockSizeCm);
	DCVoxelCraft::CraftCity(Plan, Craft, Sector.Towers);
	DCVoxelCraft::CraftTerrain(Sector.Terrain, Plan, T.City.LotSizeCm, Craft);
	OpCtx = V.OpContext(T.City.BlockSizeCm);
	Colors = V.MeshColors();
	BuiltSector = SectorIndex;
	BuiltId = BuildId;
	BuiltSeed = RunSeed;
	AppliedSeq = 0;
	QueueAllDirty(nullptr);
	UE_LOG(LogTemp, Display, TEXT("DCVOXEL build sector=%d build=%d towers=%d craft=%.0f ms hash=%08x"), SectorIndex, BuildId, Sector.Towers.Num(),
		(FPlatformTime::Seconds() - Start) * 1000.0, Sector.Hash());
}

FDCVoxelOpResult ADCVoxelCity::ApplyOp(const FDCVoxelOp& Op, bool bAnimate)
{
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		FollowGameState(); // the host may call before the first tick built the city
	}
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		UE_LOG(LogTemp, Warning, TEXT("DCVOXEL op seq=%d skipped: no city built"), Op.Seq);
		return FDCVoxelOpResult();
	}
	const double Start = FPlatformTime::Seconds();
	FDCVoxelOpResult Result = DCVoxelOps::Apply(Sector, Op, OpCtx);
	AppliedSeq = FMath::Max(AppliedSeq, Op.Seq);
	for (int32 Lot : Result.FallenLots)
	{
		SetTowerOffset(Lot, FVector::ZeroVector); // the stump stops shaking
	}
	const float Now = GetWorld()->GetTimeSeconds();

	// The crater shows now; rubble chunks wait for their piece to land.
	TSet<FIntVector> Defer;
	if (bAnimate && Result.Pieces.Num() > 0)
	{
		TSet<FIntVector> TerrainDirty = Sector.Terrain.Data.TakeDirty();
		const FDCVoxelCarveDef& Carve = Op.Kind == TEXT("Fell") ? OpCtx.Tuning->DefaultCarve : OpCtx.CarveFor(Op.Stratagem);
		const int32 R = Carve.CraterRadiusCm + Sector.Terrain.VoxelCm;
		const FIntVector Lo = FDCVoxelWorld::ChunkOf(Sector.Terrain.ToLocal(Op.CenterCm - FIntVector(R)));
		const FIntVector Hi = FDCVoxelWorld::ChunkOf(Sector.Terrain.ToLocal(Op.CenterCm + FIntVector(R)));
		TSet<FIntVector> Crater;
		for (const FIntVector& C : TerrainDirty)
		{
			const bool bCrater = C.X >= Lo.X && C.X <= Hi.X && C.Y >= Lo.Y && C.Y <= Hi.Y && C.Z >= Lo.Z && C.Z <= Hi.Z;
			(bCrater ? Crater : Defer).Add(C);
		}
		QueueDirty(0, Crater);
	}
	QueueAllDirty(nullptr);
	TArray<FIntVector> DeferList = Defer.Array();
	for (const FDCVoxelPiece& Piece : Result.Pieces)
	{
		if (bAnimate)
		{
			// Where the piece's rubble lies: the landing dust and the decay smoke and fire sit there.
			FVector Sum = FVector::ZeroVector;
			for (int32 i = Piece.RubbleStart; i < Piece.RubbleStart + Piece.RubbleNum; ++i)
			{
				Sum += Sector.Terrain.ToWorld(Result.Rubble[i]);
			}
			const FVector Land = Piece.RubbleNum > 0 ? Sum / Piece.RubbleNum : Sector.Towers[Piece.Lot].Grid.ToWorld(Piece.Pivot);
			StartFall(Piece, Now, DeferList, Land);
			DeferList.Reset(); // the first piece to land shows the rubble
		}
	}
	if (bAnimate && Fx && Op.Kind != TEXT("Fell"))
	{
		const FDCVoxelCarveDef& Carve = OpCtx.CarveFor(Op.Stratagem);
		Fx->Emit(TEXT("Crater"), FVector(Op.CenterCm), Carve.CraterRadiusCm);
		Fx->Burn(TEXT("CraterFire"), FVector(Op.CenterCm), Carve.CraterRadiusCm * 0.6f, OpCtx.Tuning->DecaySeconds * 0.5f);
	}
	for (const FIntVector& C : DeferList)
	{
		DeferredTerrain.Add({Now, C});
	}
	UE_LOG(LogTemp, Display, TEXT("DCVOXEL op seq=%d kind=%s lot=%d carved=%d fallen=%d pieces=%d rubble=%d %.0f ms hash=%08x%s"), Op.Seq, *Op.Kind.ToString(), Op.Lot,
		Result.Carved, Result.FallenLots.Num(), Result.Pieces.Num(), Result.Rubble.Num(), (FPlatformTime::Seconds() - Start) * 1000.0, Sector.Hash(),
		Op.ResultHash == 0 ? TEXT("") : (Op.ResultHash == Sector.Hash() ? TEXT(" match=host") : TEXT(" MISMATCH=host")));
	return Result;
}

void ADCVoxelCity::StartFall(const FDCVoxelPiece& Piece, float Now, const TArray<FIntVector>& Deferred, const FVector& LandAt)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const FDCVoxelGrid& Grid = Sector.Towers[Piece.Lot].Grid;
	TSharedPtr<FFalling> F = MakeShared<FFalling>();
	F->Pivot = Grid.ToWorld(Piece.Pivot);
	F->Start = Now;
	F->Duration = FMath::Max(Tn.FallSeconds * (Piece.bTopple ? 1.f : 0.6f), 0.2f);
	// Leans over away from the blast: rotation about the horizontal axis perpendicular to the fall direction.
	F->Axis = FVector::CrossProduct(FVector::UpVector, FVector(Piece.Dir)).GetSafeNormal();
	F->MaxAngle = Piece.bTopple ? Tn.ToppleDegrees : 0.f;
	const float PivotHeight = F->Pivot.Z;
	F->DropCm = Piece.bTopple ? FMath::Max(PivotHeight * 0.5f, 400.f) : FMath::Max(PivotHeight, 300.f);
	F->LandCenter = LandAt;
	const uint8 Mat = Piece.Materials.Num() > 0 ? Piece.Materials[0] : 1;
	F->Color = Colors.IsValidIndex(Mat) ? FLinearColor::FromSRGBColor(Colors[Mat]) : FLinearColor::Black;
	F->Comp = NewMesh(false);
	F->Comp->SetWorldLocation(F->Pivot);
	F->Comp->SetVisibility(false);
	Falling.Add(F);
	for (const FIntVector& C : Deferred)
	{
		DeferredTerrain.Add({Now + F->Duration, C});
	}

	// Mesh the piece off the game thread (cells relative to the hinge, so rotating the component tips it over).
	TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
	Job->Grid = INDEX_NONE;
	Job->BuildId = BuiltId;
	Job->Piece = F;
	const float Vcm = Grid.VoxelCm;
	TArray<FIntVector> PieceCells = Piece.Cells;
	TArray<uint8> Mats = Piece.Materials;
	const FIntVector Pivot = Piece.Pivot;
	TArray<FColor> PieceColors = Colors;
	FDCVoxelMeshData* Out = &Job->Mesh;
	Job->Task = UE::Tasks::Launch(TEXT("DCVoxelPieceMesh"), [PieceCells = MoveTemp(PieceCells), Mats = MoveTemp(Mats), Pivot, PieceColors = MoveTemp(PieceColors), Vcm, Out]()
	{
		DCVoxelMesher::MeshCells(PieceCells, Mats, Pivot, PieceColors, Vcm, *Out);
	});
	Jobs.Add(Job);
	DCAudio::PlayAt(this, TEXT("Creak"), F->Pivot, 1.f);
}

void ADCVoxelCity::QueueDirty(int32 GridIndex, const TSet<FIntVector>& Chunks)
{
	const FDCVoxelGrid& Grid = GridAt(GridIndex);
	const float Vcm = Grid.VoxelCm;
	const float AO = UDCVoxelSettings::Get().Tuning.AOStrength;
	for (const FIntVector& Chunk : Chunks)
	{
		TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
		Job->Grid = GridIndex;
		Job->Chunk = Chunk;
		Job->BuildId = BuiltId;
		TArray<uint8> Padded;
		Grid.Data.CopyPadded(Chunk, Padded); // a snapshot: the job never reads the live grid
		FDCVoxelMeshData* Out = &Job->Mesh;
		TArray<FColor> JobColors = Colors;
		Job->Task = UE::Tasks::Launch(TEXT("DCVoxelChunkMesh"), [Padded = MoveTemp(Padded), JobColors = MoveTemp(JobColors), Vcm, AO, Out]()
		{
			DCVoxelMesher::MeshPadded(Padded, JobColors, Vcm, AO, *Out);
		});
		Jobs.Add(Job);
	}
}

void ADCVoxelCity::QueueAllDirty(TSet<FIntVector>* DeferTerrain)
{
	for (int32 G = 0; G < NumGrids(); ++G)
	{
		FDCVoxelGrid& Grid = GridAt(G);
		if (Grid.Data.HasDirty())
		{
			QueueDirty(G, Grid.Data.TakeDirty());
		}
	}
}

void ADCVoxelCity::PollJobs()
{
	for (int32 i = 0; i < Jobs.Num();)
	{
		const TSharedPtr<FMeshJob> Job = Jobs[i];
		if (!Job->Task.IsCompleted())
		{
			++i;
			continue;
		}
		Jobs.RemoveAtSwap(i);
		if (Job->BuildId != BuiltId)
		{
			continue; // a stale job from a previous build
		}
		if (Job->Piece)
		{
			FFalling& F = *Job->Piece;
			if (F.Comp && !F.bLanded)
			{
				F.Comp->CreateMeshSection(0, Job->Mesh.Positions, Job->Mesh.Indices, Job->Mesh.Normals, Job->Mesh.UVs, Job->Mesh.Colors, {}, false);
				F.Comp->SetVisibility(true);
				F.bReady = true;
			}
			continue;
		}
		const FIntVector4 K = Key(Job->Grid, Job->Chunk);
		const FIntVector4 Cell = Key(Job->Grid, CellOf(Job->Grid, Job->Chunk));
		if (Job->Mesh.IsEmpty())
		{
			ChunkMeshes.Remove(K);
		}
		else
		{
			ChunkMeshes.Add(K, MoveTemp(Job->Mesh));
		}
		CellChunks.FindOrAdd(Cell).Add(Job->Chunk);
		DirtyCells.Add(Cell);
	}
}

void ADCVoxelCity::UploadCells()
{
	if (DirtyCells.Num() == 0)
	{
		return;
	}
	// A cell uploads only when none of its chunks still has a job in flight (no half-updated cells).
	TSet<FIntVector4> Busy;
	for (const TSharedPtr<FMeshJob>& Job : Jobs)
	{
		if (Job->Grid != INDEX_NONE)
		{
			Busy.Add(Key(Job->Grid, CellOf(Job->Grid, Job->Chunk)));
		}
	}
	int32 Budget = FMath::Max(1, UDCVoxelSettings::Get().Tuning.UploadsPerFrame);
	TArray<FIntVector4> Ready;
	for (const FIntVector4& Cell : DirtyCells)
	{
		if (!Busy.Contains(Cell))
		{
			Ready.Add(Cell);
			if (Ready.Num() >= Budget)
			{
				break;
			}
		}
	}
	FDCVoxelMeshData Merged;
	for (const FIntVector4& Cell : Ready)
	{
		DirtyCells.Remove(Cell);
		Merged.Reset();
		const int32 G = Cell.X;
		const float ChunkCm = FDCVoxelChunk::Size * GridAt(G).VoxelCm;
		for (const FIntVector& Chunk : CellChunks.FindRef(Cell))
		{
			const FDCVoxelMeshData* M = ChunkMeshes.Find(Key(G, Chunk));
			if (!M)
			{
				continue;
			}
			const FVector Offset = FVector(Chunk) * ChunkCm;
			const int32 Base = Merged.Positions.Num();
			for (const FVector& P : M->Positions)
			{
				Merged.Positions.Add(P + Offset);
			}
			Merged.Normals.Append(M->Normals);
			Merged.UVs.Append(M->UVs);
			Merged.Colors.Append(M->Colors);
			for (int32 Index : M->Indices)
			{
				Merged.Indices.Add(Base + Index);
			}
		}
		TObjectPtr<UProceduralMeshComponent>& Mesh = Cells.FindOrAdd(Cell);
		if (Merged.IsEmpty())
		{
			if (Mesh)
			{
				Mesh->ClearAllMeshSections();
			}
			continue;
		}
		if (!Mesh)
		{
			Mesh = NewMesh(true);
			Mesh->SetRelativeLocation(GridAt(G).ToWorld(FIntVector::ZeroValue));
			Mesh->ComponentTags.Add(G == 0 ? FName(TEXT("Terrain")) : FName(*FString::Printf(TEXT("Lot%d"), G - 1)));
		}
		Mesh->CreateMeshSection(0, Merged.Positions, Merged.Indices, Merged.Normals, Merged.UVs, Merged.Colors, {}, true);
	}
}

void ADCVoxelCity::SetTowerOffset(int32 Lot, const FVector& Offset)
{
	if (!Sector.Towers.IsValidIndex(Lot))
	{
		return;
	}
	const FVector Base = Sector.Towers[Lot].Grid.ToWorld(FIntVector::ZeroValue);
	for (const TPair<FIntVector4, TObjectPtr<UProceduralMeshComponent>>& Pair : Cells)
	{
		if (Pair.Key.X == Lot + 1 && Pair.Value)
		{
			Pair.Value->SetRelativeLocation(Base + Offset);
		}
	}
}

void ADCVoxelCity::TickFalling(float Now)
{
	for (int32 i = Falling.Num() - 1; i >= 0; --i)
	{
		FFalling& F = *Falling[i];
		const float Age = FMath::Max(Now - F.Start, 0.f);
		const float U = FMath::Clamp(Age / F.Duration, 0.f, 1.f);
		if (F.Comp)
		{
			// Drops from the first frame under (exaggerated) gravity, so the top never hangs over the bitten floors,
			// and leans away from the blast as it goes.
			const float Drop = FMath::Min(0.5f * UDCVoxelSettings::Get().Tuning.FallGravityCm * Age * Age, F.DropCm);
			F.Comp->SetWorldRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.MaxAngle * U * U)));
			F.Comp->SetWorldLocation(F.Pivot - FVector(0, 0, Drop));
		}
		if (U >= 1.f)
		{
			Land(F);
			Falling.RemoveAtSwap(i);
		}
	}
	// Rubble chunks show as their piece lands.
	TSet<FIntVector> Show;
	for (int32 i = DeferredTerrain.Num() - 1; i >= 0; --i)
	{
		if (Now >= DeferredTerrain[i].Key)
		{
			Show.Add(DeferredTerrain[i].Value);
			DeferredTerrain.RemoveAtSwap(i);
		}
	}
	if (Show.Num() > 0)
	{
		QueueDirty(0, Show);
	}
}

void ADCVoxelCity::Land(FFalling& F)
{
	F.bLanded = true;
	const FVector Where = F.LandCenter + FVector(0, 0, 100.f);
	if (Fx)
	{
		const float Radius = UDCVoxelSettings::Get().Tuning.TerrainVoxelCm * 12.f;
		Fx->Emit(TEXT("Landing"), Where, Radius);
		Fx->Burn(TEXT("Rubble"), Where, Radius, UDCVoxelSettings::Get().Tuning.DecaySeconds);
	}
	if (F.Comp)
	{
		MeshPool.Remove(F.Comp);
		F.Comp->DestroyComponent();
		F.Comp = nullptr;
	}
	// Flying voxel chunks hide the swap from the falling mesh to the settled rubble.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr;
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (Pool)
	{
		FRandomStream Rng(GetTypeHash(Where));
		const float S = Sector.Terrain.VoxelCm / 100.f;
		for (int32 i = 0; i < Tn.LandingDebris; ++i)
		{
			FDCDebrisSpawn D;
			D.Transform = FTransform(FRotator(Rng.FRandRange(0, 360), Rng.FRandRange(0, 360), 0), Where + Rng.VRand() * FVector(600, 600, 200), FVector(S * Rng.FRandRange(0.15f, 0.5f)));
			D.LinearVelocity = Rng.VRand() * FVector(700, 700, 0) + FVector(0, 0, Rng.FRandRange(300, 900));
			D.AngularVelocityDeg = Rng.VRand() * 300.f;
			D.Color = F.Color * Rng.FRandRange(0.7f, 1.3f);
			D.LifeSeconds = UDCTuning::Get().City.DebrisLifeSeconds * Rng.FRandRange(0.4f, 0.8f);
			Pool->Spawn(D);
		}
	}
	DCAudio::PlayAt(this, TEXT("Collapse"), Where, 1.f);
}

void ADCVoxelCity::FollowGameState()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS)
	{
		return;
	}
	if (GS->CityBuildId != BuiltId || (GS->CitySector == INDEX_NONE) != (BuiltSector == INDEX_NONE))
	{
		if (GS->CitySector == INDEX_NONE)
		{
			Clear();
			BuiltId = GS->CityBuildId;
		}
		else
		{
			Build(GS->RunSeed, GS->CitySector, GS->CityBuildId);
		}
	}
	if (BuiltSector == INDEX_NONE)
	{
		return;
	}
	// Ops of this build, in Seq order; late joiners (or ops already old) skip the fall animation.
	const float Now = GS->Now();
	for (const FDCVoxelOp& Op : GS->VoxelOps)
	{
		if (Op.Build == BuiltId && Op.Seq > AppliedSeq)
		{
			ApplyOp(Op, Now - Op.Time < 2.f);
		}
	}
}

void ADCVoxelCity::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFollowGameState) // the host drives Build and ApplyOp directly first, so following is a no-op there
	{
		FollowGameState();
	}
	PollJobs();
	UploadCells();
	TickFalling(GetWorld()->GetTimeSeconds());
}
