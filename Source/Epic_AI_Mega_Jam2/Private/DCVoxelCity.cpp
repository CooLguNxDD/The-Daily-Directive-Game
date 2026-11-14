#include "DCVoxelCity.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "DCVoxelFx.h"
#include "DCCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"

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
#include "Voxel/DCVoxelModel.h"

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
	const int32 Seed = DCCityRules::SectorSeed(RunSeed, SectorIndex);
	if (IsEnabled())
	{
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		// The city builder (R16) when the sector has a district row; recipes on the block grid otherwise.
		if (const FDCDistrictRule* D = V.District(SectorIndex))
		{
			return DCVoxelCraft::PlanDistrict(Seed, *D, V.RuleSet(D->Rules), V.RuleSet(D->Props), V.SectorLayout(SectorIndex), V.Recipes, T.City.BlockSizeCm);
		}
		FDCCityPlan Plan = DCCityRules::PlanCity(Seed, T.City, T.Look.BuildingColors.Num());
		DCVoxelCraft::ApplyRecipes(Plan, V.SectorLayout(SectorIndex), V.Recipes, FMath::FloorToInt(T.City.LotSizeCm / T.City.BlockSizeCm));
		return Plan;
	}
	return DCCityRules::PlanCity(Seed, T.City, T.Look.BuildingColors.Num());
}

float ADCVoxelCity::SurfaceZCm(const FVector& Where) const
{
	const FDCVoxelGrid& T = Sector.Terrain;
	if (BuiltSector == INDEX_NONE)
	{
		return 0.f;
	}
	const FIntVector C = T.ToLocal(DCVoxelMath::ToCm(FVector(Where.X, Where.Y, 0.f)));
	if (InFlight.IsValid())
	{
		// An op plans or waits: inside its area the snapshot taken before it started; outside it the sector is
		// unchanged, but it may only be read once the worker is done writing.
		if (HoldRadiusCm >= 0.f && FVector2D::DistSquared(FVector2D(Where.X, Where.Y), HoldCenterCm) <= FMath::Square(HoldRadiusCm))
		{
			const float* Held = HoldTops.Find(FIntPoint(C.X, C.Y));
			return Held ? *Held : 0.f;
		}
		if (!InFlightTask.IsCompleted())
		{
			const float* Cached = SurfaceCache.Find(FIntPoint(C.X, C.Y));
			return Cached ? *Cached : 0.f;
		}
	}
	if (const float* Cached = SurfaceCache.Find(FIntPoint(C.X, C.Y)))
	{
		return *Cached;
	}
	const float Top = ScanSurface(C);
	SurfaceCache.Add(FIntPoint(C.X, C.Y), Top);
	return Top;
}

float ADCVoxelCity::ScanSurface(const FIntVector& C) const
{
	const FDCVoxelGrid& T = Sector.Terrain;
	for (int32 Z = UDCVoxelSettings::Get().Tuning.GroundLayers + 60; Z >= 0; --Z)
	{
		if (T.Data.IsSolid(FIntVector(C.X, C.Y, Z)))
		{
			return static_cast<float>(T.OriginCm.Z + (Z + 1) * T.VoxelCm);
		}
	}
	return 0.f;
}

bool ADCVoxelCity::IsSightBlocked(const FVector& From, const FVector& To, const TArray<int32>& IgnoreLots) const
{
	if (BuiltSector == INDEX_NONE || (InFlight.IsValid() && !InFlightTask.IsCompleted()))
	{
		return false;
	}
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	return DCVoxelOps::SightBlocked(Sector, From, To, IgnoreLots, Tn.SightStepCm, Tn.SightSkipNearCm, Tn.SightSkipFarCm);
}

void ADCVoxelCity::LotsNear(const FVector& Where, float RadiusCm, TArray<int32>& Out) const
{
	Out.Reset();
	const FIntVector C = DCVoxelMath::ToCm(Where);
	for (int32 i = 0; i < Sector.Towers.Num(); ++i)
	{
		const FIntVector A = Sector.Towers[i].FootMinCm(), B = Sector.Towers[i].FootMaxCm();
		const float Dx = FMath::Max3(static_cast<float>(A.X - C.X), 0.f, static_cast<float>(C.X - B.X));
		const float Dy = FMath::Max3(static_cast<float>(A.Y - C.Y), 0.f, static_cast<float>(C.Y - B.Y));
		if (Dx * Dx + Dy * Dy <= RadiusCm * RadiusCm)
		{
			Out.Add(i);
		}
	}
}

bool ADCVoxelCity::HasPendingOpsAt(float Time) const
{
	if (InFlight.IsValid() && InFlight->Op.Time <= Time)
	{
		return true;
	}
	return QueuedOps.ContainsByPredicate([Time](const FQueuedOp& Q) { return Q.Op.Time <= Time; });
}

bool ADCVoxelCity::IsSolidAt(const FVector& Where) const
{
	if (BuiltSector == INDEX_NONE)
	{
		return false;
	}
	const FIntVector Cm = DCVoxelMath::ToCm(Where);
	if (InFlight.IsValid())
	{
		// An op writes the voxels on a worker: answer conservatively from what does not change under it, the cached
		// street surface and each tower's crafted shell box (a falling tower still counts), so the lens still backs out.
		if (Where.Z < SurfaceZCm(Where))
		{
			return true;
		}
		for (const FDCVoxelTower& T : Sector.Towers)
		{
			const FIntVector L = T.Grid.ToLocal(Cm);
			if (L.X >= T.Min.X && L.Y >= T.Min.Y && L.Z >= 0 && L.X < T.Max.X && L.Y < T.Max.Y && L.Z < T.Max.Z)
			{
				return true;
			}
		}
		return false;
	}
	if (Sector.Terrain.Data.IsSolid(Sector.Terrain.ToLocal(Cm)) || Sector.Props.Data.IsSolid(Sector.Props.ToLocal(Cm)))
	{
		return true;
	}
	for (const FDCVoxelTower& T : Sector.Towers)
	{
		const FIntVector L = T.Grid.ToLocal(Cm);
		const bool bInBox = L.X >= T.ScanMin.X && L.Y >= T.ScanMin.Y && L.Z >= 0 && L.X < T.ScanMax.X && L.Y < T.ScanMax.Y && L.Z < T.ScanMax.Z;
		// Inside the shell counts as inside: the hollow interior photographs as a dark box.
		if (bInBox && (T.Grid.Data.IsSolid(L) || (!T.bFallen && L.X >= T.Min.X && L.Y >= T.Min.Y && L.X < T.Max.X && L.Y < T.Max.Y && L.Z < T.Max.Z)))
		{
			return true;
		}
	}
	return false;
}

const FDCSeasonDef* ADCVoxelCity::SeasonFor(int32 RunSeed, int32 SectorIndex)
{
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const FDCDistrictRule* D = V.District(SectorIndex);
	return DCWeather::SeasonFor(UDCTuning::Get().Seasons, D ? V.RuleSet(D->Weather) : nullptr, DCCityRules::SectorSeed(RunSeed, SectorIndex));
}

FDCWeatherState ADCVoxelCity::WeatherAt(int32 RunSeed, int32 SectorIndex, float Fraction)
{
	const FDCSeasonDef* Season = SeasonFor(RunSeed, SectorIndex);
	const FDCWeatherArc* Arc = Season ? DCWeather::ArcFor(*Season, DCCityRules::SectorSeed(RunSeed, SectorIndex)) : nullptr;
	return Season && Arc ? DCWeather::At(UDCTuning::Get().WeatherProfiles, *Season, *Arc, Fraction) : FDCWeatherState();
}

namespace
{
	/** Winter: a layer of snow on exposed street tops and roof slabs, by hash (same on every machine). */
	void LaySnow(FDCVoxelSector& Sector, float Cover, uint8 Snow, int32 Seed)
	{
		if (Cover <= 0.f || Snow == 0)
		{
			return;
		}
		const uint32 Threshold = static_cast<uint32>(FMath::Clamp(Cover, 0.f, 1.f) * 1000.f);
		FDCVoxelGrid& T = Sector.Terrain;
		const int32 Top = UDCVoxelSettings::Get().Tuning.GroundLayers - 1;
		const FIntVector Lo = T.ToLocal(T.OriginCm);
		const int32 N = 2 * FMath::Abs(T.OriginCm.X) / FMath::Max(T.VoxelCm, 1);
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FIntVector P(Lo.X + X, Lo.Y + Y, Top);
				// Organic drifts: smooth value noise over 3 m cells (bilinear between hashed corners), not a checkerboard.
				constexpr int32 Cell = 6;
				auto Corner = [Seed](int32 Cx, int32 Cy) { return static_cast<float>(DCPcg::Hash(Seed, 0x5A0Du, Cx * 4099 + Cy) % 1000) / 1000.f; };
				const int32 Cx = X / Cell, Cy = Y / Cell;
				const float Fx = FMath::SmoothStep(0.f, 1.f, static_cast<float>(X % Cell) / Cell), Fy = FMath::SmoothStep(0.f, 1.f, static_cast<float>(Y % Cell) / Cell);
				const float Noise = FMath::Lerp(FMath::Lerp(Corner(Cx, Cy), Corner(Cx + 1, Cy), Fx), FMath::Lerp(Corner(Cx, Cy + 1), Corner(Cx + 1, Cy + 1), Fx), Fy);
				if (T.Data.IsSolid(P) && !T.Data.IsSolid(P + FIntVector(0, 0, 1)) && Noise * 1000.f < Threshold)
				{
					T.Data.Set(P, Snow); // the street top whitens (50 cm cells stay flush)
				}
			}
		}
		for (FDCVoxelTower& Tw : Sector.Towers)
		{
			const int32 Roof = Tw.Max.Z;
			for (int32 Y = Tw.ScanMin.Y; Y < Tw.ScanMax.Y; ++Y)
			{
				for (int32 X = Tw.ScanMin.X; X < Tw.ScanMax.X; ++X)
				{
					const FIntVector P(X, Y, Roof);
					if (Tw.Grid.Data.IsSolid(P) && !Tw.Grid.Data.IsSolid(P + FIntVector(0, 0, 1)) && DCPcg::Hash(Seed, 0x5A0Eu, X * 4099 + Y) % 1000 < Threshold)
					{
						Tw.Grid.Data.Set(P + FIntVector(0, 0, 1), Snow);
					}
				}
			}
		}
	}
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

UMaterialInterface* ADCVoxelCity::VoxelMaterialFor(int32 VoxelCm)
{
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = VoxelMIDs.Find(VoxelCm))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(VoxelMaterial(), this);
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	Mid->SetScalarParameterValue(TEXT("VoxelCm"), static_cast<float>(VoxelCm));
	Mid->SetScalarParameterValue(TEXT("EdgeWidth"), Tn.VoxelEdgeWidth);
	Mid->SetScalarParameterValue(TEXT("EdgeDark"), Tn.VoxelEdgeDark);
	Mid->SetScalarParameterValue(TEXT("Jitter"), Tn.VoxelJitter);
	Mid->SetScalarParameterValue(TEXT("Wetness"), Wetness);
	VoxelMIDs.Add(VoxelCm, Mid);
	return Mid;
}

void ADCVoxelCity::SetWetness(float InWetness)
{
	Wetness = FMath::Clamp(InWetness, 0.f, 1.f);
	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& It : VoxelMIDs)
	{
		if (It.Value)
		{
			It.Value->SetScalarParameterValue(TEXT("Wetness"), Wetness);
		}
	}
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
	if (InFlight.IsValid())
	{
		InFlightTask.Wait();
		InFlight.Reset();
		bHoldUploads = false; // a dropped held op never blocks later uploads
		HeldCells.Reset();
		CommitCells.Reset();
	}
	for (const TSharedPtr<FMeshJob>& Job : Jobs)
	{
		Job->Task.Wait(); // jobs only touch their own data, but must not outlive the actor
	}
	Jobs.Reset();
	Super::EndPlay(Reason);
}

FDCVoxelGrid& ADCVoxelCity::GridAt(int32 GridIndex)
{
	return const_cast<FDCVoxelGrid&>(static_cast<const ADCVoxelCity*>(this)->GridAt(GridIndex));
}

const FDCVoxelGrid& ADCVoxelCity::GridAt(int32 GridIndex) const
{
	// 0 = terrain, 1..N = towers, N + 1 = props.
	if (GridIndex == 0)
	{
		return Sector.Terrain;
	}
	return Sector.Towers.IsValidIndex(GridIndex - 1) ? Sector.Towers[GridIndex - 1].Grid : Sector.Props;
}

FIntVector ADCVoxelCity::CellOf(int32 GridIndex, const FIntVector& Chunk) const
{
	// Render cells are sized in cm (never smaller than one chunk), so the component count stays flat across voxel sizes.
	const int32 ChunkCm = FDCVoxelChunk::Size * GridAt(GridIndex).VoxelCm;
	const int32 PerCell = FMath::Max(1, UDCVoxelSettings::Get().Tuning.RenderCellCm / ChunkCm);
	return DCVoxelMath::FloorDiv(Chunk, PerCell);
}

UProceduralMeshComponent* ADCVoxelCity::NewMesh(bool bCollision, int32 VoxelCm)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->bUseAsyncCooking = true;
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
	Mesh->SetCastShadow(true);
	Mesh->RegisterComponent();
	Mesh->SetMaterial(0, VoxelMaterialFor(VoxelCm));
	MeshPool.Add(Mesh);
	return Mesh;
}

void ADCVoxelCity::Clear()
{
	CommittedFallen.Reset();
	HoldRadiusCm = -1.f;
	HoldTops.Reset();
	if (InFlight.IsValid())
	{
		InFlightTask.Wait();
		InFlight.Reset();
		bHoldUploads = false; // a dropped held op never blocks later uploads
		HeldCells.Reset();
		CommitCells.Reset();
	}
	QueuedOps.Reset();
	SurfaceCache.Reset();
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
	Sector.Props.Data.Reset();
	BuiltSector = INDEX_NONE;
	AppliedSeq = 0;
}

void ADCVoxelCity::Build(int32 RunSeed, int32 SectorIndex, int32 BuildId)
{
	CommittedFallen.Reset();
	HoldRadiusCm = -1.f;
	HoldTops.Reset();
	Clear();
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const UDCTuning& T = UDCTuning::Get();
	const double Start = FPlatformTime::Seconds();
	const FDCCityPlan Plan = PlanSector(RunSeed, SectorIndex);
	FDCVoxelCraftContext Craft = V.CraftContext(T.City.BlockSizeCm);
	for (const FDCPropPlan& Prop : Plan.Props)
	{
		if (!Craft.Models.Contains(Prop.Model))
		{
			Craft.Models.Add(Prop.Model, DCVoxelModels::Parsed(Prop.Model)); // file reads stay on the game thread
		}
	}
	DCVoxelCraft::CraftCity(Plan, Craft, Sector.Towers);
	DCVoxelCraft::CraftTerrain(Sector.Terrain, Plan, T.City.LotSizeCm, Craft);
	DCVoxelCraft::CraftRubbleZones(Sector.Terrain, Plan, Craft);
	DCVoxelCraft::CraftProps(Sector.Props, Plan, Craft);
	if (const FDCSeasonDef* Season = SeasonFor(RunSeed, SectorIndex))
	{
		LaySnow(Sector, Season->SnowCover, Craft.MaterialId(TEXT("Snow")), DCCityRules::SectorSeed(RunSeed, SectorIndex));
	}
	OpCtx = V.OpContext(T.City.BlockSizeCm);
	Colors = V.MeshColors();
	BuiltSector = SectorIndex;
	BuiltId = BuildId;
	BuiltSeed = RunSeed;
	AppliedSeq = 0;
	QueueAllDirty(nullptr);
	bLoading = true;
	UE_LOG(LogTemp, Display, TEXT("DCVOXEL build sector=%d build=%d towers=%d craft=%.0f ms hash=%08x"), SectorIndex, BuildId, Sector.Towers.Num(),
		(FPlatformTime::Seconds() - Start) * 1000.0, Sector.Hash());
}

FDCVoxelOpResult ADCVoxelCity::ApplyOp(const FDCVoxelOp& Op, bool bAnimate)
{
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		SyncBuild(); // build only: following would also apply this very op, and the caller applies it again below
	}
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		UE_LOG(LogTemp, Warning, TEXT("DCVOXEL op seq=%d skipped: no city built"), Op.Seq);
		return FDCVoxelOpResult();
	}
	WaitInFlight(); // the synchronous path (reel re-staging) never overlaps a queued op
	const double Start = FPlatformTime::Seconds();
	FDCVoxelOpResult Result = DCVoxelOps::Apply(Sector, Op, OpCtx);
	AppliedSeq = FMath::Max(AppliedSeq, Op.Seq);
	const uint32 Hash = Sector.Hash();
	FinishOp(Op, Result, bAnimate, static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0), Hash, false);
	return Result;
}

void ADCVoxelCity::ApplyOpAsync(const FDCVoxelOp& Op, bool bAnimate, TFunction<void(const FDCVoxelOpResult&, uint32)> OnDone)
{
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		SyncBuild();
	}
	if (BuiltSector == INDEX_NONE || !OpCtx.Tuning)
	{
		UE_LOG(LogTemp, Warning, TEXT("DCVOXEL op seq=%d skipped: no city built"), Op.Seq);
		return;
	}
	AppliedSeq = FMath::Max(AppliedSeq, Op.Seq); // queued: following never queues it twice
	FQueuedOp& Q = QueuedOps.AddDefaulted_GetRef();
	Q.Op = Op;
	Q.bAnimate = bAnimate;
	Q.OnDone = MoveTemp(OnDone);
	StartNextOp();
}

void ADCVoxelCity::StartNextOp()
{
	if (InFlight.IsValid() || QueuedOps.Num() == 0)
	{
		return;
	}
	// One writer: the op runs on a worker while the game thread leaves the sector alone (readers use caches), so a
	// collapse costs the game thread only the uploads (impact hitch, R13). Ops stay strictly in Seq order.
	InFlight = MakeShared<FQueuedOp>(MoveTemp(QueuedOps[0]));
	QueuedOps.RemoveAt(0);
	// Snapshot the street tops the op can change (blast radius plus topple reach), before the worker writes them.
	{
		const FDCVoxelOp& Op = InFlight->Op;
		const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
		HoldCenterCm = FVector2D(Op.CenterCm.X, Op.CenterCm.Y);
		HoldRadiusCm = FMath::Max(Op.BlastRadiusCm, 0) + Tn.HoldReachCm;
		HoldTops.Reset();
		const FDCVoxelGrid& T = Sector.Terrain;
		const int32 R = FMath::CeilToInt32(HoldRadiusCm / T.VoxelCm) + 1;
		const FIntVector Mid = T.ToLocal(FIntVector(Op.CenterCm.X, Op.CenterCm.Y, 0));
		for (int32 Y = Mid.Y - R; Y <= Mid.Y + R; ++Y)
		{
			for (int32 X = Mid.X - R; X <= Mid.X + R; ++X)
			{
				const FIntVector Col(X, Y, 0);
				const float* Cached = SurfaceCache.Find(FIntPoint(X, Y));
				HoldTops.Add(FIntPoint(X, Y), Cached ? *Cached : ScanSurface(Col));
			}
		}
	}
	TSharedPtr<FQueuedOp> Job = InFlight;
	FDCVoxelSector* Target = &Sector;
	const FDCVoxelOpContext* Ctx = &OpCtx;
	InFlightTask = UE::Tasks::Launch(TEXT("DCVoxelOp"), [Job, Target, Ctx]()
	{
		const double Start = FPlatformTime::Seconds();
		Job->Result = DCVoxelOps::Apply(*Target, Job->Op, *Ctx);
		Job->Hash = Target->Hash();
		Job->ApplyMs = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	});
}

void ADCVoxelCity::WaitInFlight()
{
	if (!InFlight.IsValid())
	{
		return;
	}
	InFlightTask.Wait();
	FinishInFlight();
}

void ADCVoxelCity::FinishInFlight()
{
	TSharedPtr<FQueuedOp> Done = InFlight;
	InFlight.Reset();
	SurfaceCache.Reset();
	if (Done->bPrepared)
	{
		bHoldUploads = false;
		CommitCells.Append(HeldCells);
		HeldCells.Reset();
		CommitOp(Done->Op, Done->Result, Done->bAnimate, Done->ApplyMs, Done->Hash, true);
	}
	else
	{
		FinishOp(Done->Op, Done->Result, Done->bAnimate, Done->ApplyMs, Done->Hash, true);
	}
	if (Done->OnDone)
	{
		Done->OnDone(Done->Result, Done->Hash);
	}
}

void ADCVoxelCity::FinishOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float ApplyMs, uint32 Hash, bool bOffThread)
{
	PrepareOp(Op, Result, bAnimate, GetWorld()->GetTimeSeconds());
	CommitOp(Op, Result, bAnimate, ApplyMs, Hash, bOffThread);
}

void ADCVoxelCity::CommitOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float ApplyMs, uint32 Hash, bool bOffThread)
{
	HoldRadiusCm = -1.f;
	HoldTops.Reset();
	CommittedFallen.SetNum(FMath::Max(CommittedFallen.Num(), Sector.Towers.Num()));
	for (int32 Lot : Result.FallenLots)
	{
		CommittedFallen[Lot] = true;
		SetTowerOffset(Lot, FVector::ZeroVector); // the stump stops shaking
	}
	// Host: nobody ends up inside the new rubble; lift them onto it (movement replicates to clients).
	if (HasAuthority() && GetNetMode() != NM_Client && bFollowGameState)
	{
		for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
		{
			const float Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Surface = SurfaceZCm(C->GetActorLocation());
			if (C->GetActorLocation().Z - Half < Surface - 10.f)
			{
				C->SetActorLocation(FVector(C->GetActorLocation().X, C->GetActorLocation().Y, Surface + Half + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}
	if (bAnimate && Fx && Op.Kind != TEXT("Fell"))
	{
		const FDCVoxelCarveDef& Carve = OpCtx.CarveFor(Op.Stratagem);
		Fx->Emit(TEXT("Crater"), FVector(Op.CenterCm), Carve.CraterRadiusCm);
		Fx->Burn(TEXT("CraterFire"), FVector(Op.CenterCm), Carve.CraterRadiusCm * 0.6f, OpCtx.Tuning->DecaySeconds * 0.5f);
	}
	// commit-late: seconds between the op's apply time (a strike's detonation) and this commit (0 = on time).
	const ADCGameState* OpState = GetWorld()->GetGameState<ADCGameState>();
	const float Late = OpState ? OpState->Now() - Op.Time : 0.f;
	UE_LOG(LogTemp, Display, TEXT("DCVOXEL op seq=%d kind=%s lot=%d carved=%d fallen=%d pieces=%d rubble=%d %.0f ms off-thread=%d commit-late=%.3f hash=%08x%s"), Op.Seq, *Op.Kind.ToString(), Op.Lot,
		Result.Carved, Result.FallenLots.Num(), Result.Pieces.Num(), Result.Rubble.Num(), ApplyMs, bOffThread ? 1 : 0, Late, Hash,
		Op.ResultHash == 0 ? TEXT("") : (Op.ResultHash == Hash ? TEXT(" match=host") : TEXT(" MISMATCH=host")));
}

void ADCVoxelCity::PrepareOp(const FDCVoxelOp& Op, FDCVoxelOpResult& Result, bool bAnimate, float StartWorldTime)
{
	const float Now = StartWorldTime;
	const int32 FirstJob = Jobs.Num();
	ON_SCOPE_EXIT
	{
		for (int32 i = FirstJob; i < Jobs.Num(); ++i)
		{
			if (!Jobs[i]->Piece && Jobs[i]->Grid != INDEX_NONE)
			{
				HeldCells.Add(Key(Jobs[i]->Grid, CellOf(Jobs[i]->Grid, Jobs[i]->Chunk)));
			}
		}
	};

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
	for (const FIntVector& C : DeferList)
	{
		DeferredTerrain.Add({Now, C});
	}
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
	// The piece falls the whole way to where its rubble lies, then breaks: it never stops in the air.
	F->DropCm = FMath::Max(F->Pivot.Z - LandAt.Z, 300.f);
	F->LandCenter = LandAt;
	const uint8 Mat = Piece.Materials.Num() > 0 ? Piece.Materials[0] : 1;
	F->Color = Colors.IsValidIndex(Mat) ? FLinearColor::FromSRGBColor(Colors[Mat]) : FLinearColor::Black;
	F->Comp = NewMesh(false, Grid.VoxelCm);
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
				F.Comp->SetVisibility(GetWorld()->GetTimeSeconds() >= F.Start); // a piece meshed ahead shows at the detonation
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
		bLoading = bLoading && Jobs.Num() > 0; // loaded once the build's jobs are done and every cell is up
		return;
	}
	// A cell uploads only when none of its chunks still has a job in flight (no half-updated cells).
	TSet<FIntVector4> Busy;
	for (const TSharedPtr<FMeshJob>& Job : Jobs)
	{
		if (Job->Grid != INDEX_NONE && !Job->Piece)
		{
			Busy.Add(Key(Job->Grid, CellOf(Job->Grid, Job->Chunk)));
		}
	}
	// 1. A committed op's own cells, all at once (the detonation frame shows the whole op).
	for (auto It = CommitCells.CreateIterator(); It; ++It)
	{
		if (Busy.Contains(*It))
		{
			continue;
		}
		if (DirtyCells.Contains(*It))
		{
			UploadCell(*It);
		}
		It.RemoveCurrent();
	}
	// 2. Everything else under the millisecond budget; a held op's cells wait for its commit.
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const double Start = FPlatformTime::Seconds();
	const int32 Cap = bLoading ? MAX_int32 : FMath::Max(1, Tn.UploadsPerFrame);
	const float BudgetMs = bLoading ? Tn.UploadLoadBudgetMs : Tn.UploadBudgetMs;
	TArray<FIntVector4> Ready;
	for (const FIntVector4& Cell : DirtyCells)
	{
		if (!Busy.Contains(Cell) && !(bHoldUploads && HeldCells.Contains(Cell)))
		{
			Ready.Add(Cell);
		}
	}
	if (bLoading)
	{
		Ready.StableSort([](const FIntVector4& A, const FIntVector4& B) { return (A.X == 0) > (B.X == 0); }); // the street first
	}
	int32 Done = 0;
	for (const FIntVector4& Cell : Ready)
	{
		if (Done > 0 && (Done >= Cap || (FPlatformTime::Seconds() - Start) * 1000.0 >= BudgetMs))
		{
			break;
		}
		UploadCell(Cell);
		++Done;
	}
}

bool ADCVoxelCity::UploadCell(const FIntVector4& Cell)
{
	DirtyCells.Remove(Cell);
	FDCVoxelMeshData Merged;
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
		return false;
	}
	if (!Mesh)
	{
		Mesh = NewMesh(true, GridAt(G).VoxelCm);
		Mesh->SetRelativeLocation(GridAt(G).ToWorld(FIntVector::ZeroValue));
		Mesh->ComponentTags.Add(G == 0 ? FName(TEXT("Terrain")) : FName(*FString::Printf(TEXT("Lot%d"), G - 1)));
	}
	Mesh->CreateMeshSection(0, Merged.Positions, Merged.Indices, Merged.Normals, Merged.UVs, Merged.Colors, {}, true);
	return true;
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
		if (F.Comp && F.bReady && Now >= F.Start && !F.Comp->IsVisible())
		{
			F.Comp->SetVisibility(true);
		}
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
	if (Show.Num() > 0 && InFlight.IsValid())
	{
		for (const FIntVector& C : Show)
		{
			DeferredTerrain.Add({Now, C}); // the worker is writing: show next frame
		}
	}
	else if (Show.Num() > 0)
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

void ADCVoxelCity::SyncBuild()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || !bFollowGameState)
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
}

void ADCVoxelCity::FollowGameState()
{
	SyncBuild();
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || BuiltSector == INDEX_NONE)
	{
		return;
	}
	// Ops of this build, in Seq order; late joiners (or ops already old) skip the fall animation.
	const float Now = GS->Now();
	for (const FDCVoxelOp& Op : GS->VoxelOps)
	{
		if (Op.Build == BuiltId && Op.Seq > AppliedSeq)
		{
			ApplyOpAsync(Op, Now - Op.Time < 2.f, nullptr);
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
	// Two-phase ops: a planned op is held until its apply time (a strike's detonation), then committed.
	const ADCGameState* State = GetWorld()->GetGameState<ADCGameState>();
	if (InFlight.IsValid() && InFlightTask.IsCompleted())
	{
		const float Lead = State ? InFlight->Op.Time - State->Now() : 0.f;
		if (Lead > 0.f && !InFlight->bPrepared)
		{
			// Planned ahead: mesh it now (the worker is done writing), show it at the apply time.
			InFlight->bPrepared = true;
			bHoldUploads = true;
			PrepareOp(InFlight->Op, InFlight->Result, InFlight->bAnimate, GetWorld()->GetTimeSeconds() + Lead);
		}
		if (Lead <= 0.f)
		{
			FinishInFlight();
			StartNextOp();
		}
	}
	const double T0 = FPlatformTime::Seconds();
	PollJobs();
	const double T1 = FPlatformTime::Seconds();
	const int32 DirtyBefore = DirtyCells.Num();
	UploadCells();
	const double T2 = FPlatformTime::Seconds();
	TickFalling(GetWorld()->GetTimeSeconds());
	const double T3 = FPlatformTime::Seconds();
	TickCityShot();
	// Hitch log (R13): which stage of the city's frame went long.
	if ((T3 - T0) * 1000.0 > UDCVoxelSettings::Get().Tuning.HitchLogMs)
	{
		UE_LOG(LogTemp, Display, TEXT("DCHITCH city poll=%.1f upload=%.1f falling=%.1f ms cells=%d->%d jobs=%d"), (T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (T3 - T2) * 1000.0,
			DirtyBefore, DirtyCells.Num(), Jobs.Num());
	}
}

void ADCVoxelCity::TickCityShot()
{
	// City builder preview (R16): -dccityshot=<dir> frames the built, fully meshed sector from three aerial cameras
	// through the player's view (real render), then quits. Never active in normal play.
	static FString Dir;
	static const bool bWanted = FParse::Value(FCommandLine::Get(), TEXT("dccityshot="), Dir);
	if (!bWanted || BuiltSector == INDEX_NONE || IsMeshing() || ShotStep >= 4)
	{
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const double Real = GetWorld()->GetRealTimeSeconds();
	if (!PC || Real < NextShotReal)
	{
		return;
	}
	const float Half = Sector.Terrain.OriginCm.X < 0 ? -static_cast<float>(Sector.Terrain.OriginCm.X) : 10000.f;
	static const FVector Eyes[3] = {FVector(-0.95f, -0.95f, 0.55f), FVector(0.97f, -0.35f, 0.35f), FVector(-0.97f, 0.f, 0.06f)}; // inside the terrain margin, clear of the skyline ring
	if (ShotStep > 0)
	{
		FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("cityshot_sector%d_%d.png"), BuiltSector, ShotStep - 1), false, false);
	}
	if (ShotStep < 3)
	{
		if (!ShotCamera)
		{
			ShotCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
			ShotCamera->GetCameraComponent()->SetFieldOfView(70.f);
		}
		const FVector Eye = Eyes[ShotStep] * Half + FVector(0, 0, 200.f);
		const FVector Look = FVector(0, 0, ShotStep == 2 ? 1500.f : 0.f);
		ShotCamera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
		PC->SetViewTargetWithBlend(ShotCamera, 0.f);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("DCCITYSHOT sector=%d frames written to %s"), BuiltSector, *Dir);
		FGenericPlatformMisc::RequestExit(false);
	}
	++ShotStep;
	NextShotReal = Real + 1.5;
}
