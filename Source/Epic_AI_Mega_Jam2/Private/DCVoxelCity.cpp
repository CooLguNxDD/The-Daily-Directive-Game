#include "DCVoxelCity.h"

#include "Async/ParallelFor.h"
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
#include "Components/BoxComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "DCPresentationClock.h"
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
	/** A falling piece's mesh (no chunk), or one of its physics fragments. */
	TSharedPtr<FFalling> Piece;
	int32 Frag = INDEX_NONE;
};

/** A falling body's rest state, in order: transitions only move forward (DCVoxelCity TickBodies). */
enum class EFragState : uint8
{
	Ghost,
	Solid,
	Grounded,
	Frozen,
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
	/** R26 physics: the piece is meshed as fragments riding on Comp (an empty root) through the scripted fall; at the
	 *  impact each becomes a Chaos body. Its rubble shows when the bodies rest. */
	struct FFrag
	{
		TObjectPtr<UProceduralMeshComponent> Comp;
		TObjectPtr<UBoxComponent> Body;
		/** Bounds in cm relative to the pivot, before rotation. */
		FBox Local = FBox(ForceInit);
		float Mass = 1.f;
		/** Per-body rest state (advisor CP27). A ghost ignores the city until it nears its site, then turns solid; a
		 *  solid body on the street gets settle damping; a frozen one is kinematic where it lies. Asleep freezes from
		 *  any state (a body asleep on a frozen pile is at rest whether or not it ever turned solid). */
		EFragState State = EFragState::Ghost;
		/** BodyAge at which this body stops counting against its piece's rest: its own fall time from where it was
		 *  released plus settle (a leaf released late by a shatter gets its own time, not what is left of the piece's). */
		float Deadline = 0.f;
		float LastChip = -10.f;
		/** Chunk (Parent = INDEX_NONE) or one of a chunk's leaves. A chunk breaks off mid-fall as one body; a hard hit
		 *  shatters it into its leaves (each its own body). Leaves carry the meshes; a chunk's Comp is an empty root. */
		int32 Parent = INDEX_NONE;
		TArray<int32> Children;
		bool bSplitPending = false;
		bool bSplit = false;
		float StillFor = 0.f;
		bool IsSolid() const { return State != EFragState::Ghost; }
	};
	TArray<FFrag> Frags;
	int32 PendingMeshes = 0;
	bool bBroken = false;
	float BodyAge = 0.f;
	float StillFor = 0.f;
	uint32 Seed = 0;
	TArray<FIntVector> RevealOnRest;
	/** Which op and piece this is (the host's Rest op names them); set when the Rest op lands here. */
	int32 OpSeq = 0;
	int32 PieceIndex = INDEX_NONE;
	bool bRestNow = false;
	/** Last tick's count of bodies past their own deadline and of strays (beyond PhysicsSiteRadiusCm), for the log. */
	int32 Late = 0;
	int32 Stray = 0;
	/** Struck lot's footprint (sector cm, XY). Rest deadlines are per body (FFrag::Deadline). */
	FBox2D LotBox = FBox2D(ForceInit);
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
			return DCVoxelCraft::PlanDistrict(Seed, *D, V.RuleSet(D->Rules), V.RuleSet(D->Props), V.SectorLayout(SectorIndex), V.Recipes, T.City.BlockSizeCm, V.PropLayers);
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
	if (BuiltSector == INDEX_NONE)
	{
		return false;
	}
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (InFlight.IsValid())
	{
		// An op is on the worker or held for its apply time: its voxels are either changing or already the future. The
		// shutter saw the city as last committed, so march the committed-intact towers as their crafted shell boxes
		// (CP7: never "clear" while an op runs, never through walls that still stood at the shutter).
		const FVector D = To - From;
		const float Len = D.Size();
		const float Step = FMath::Max(Tn.SightStepCm, 1.f);
		for (float S = Tn.SightSkipNearCm; S < Len - Tn.SightSkipFarCm; S += Step)
		{
			const FIntVector Cm = DCVoxelMath::ToCm(From + D * (S / FMath::Max(Len, 1.f)));
			for (int32 Lot = 0; Lot < Sector.Towers.Num(); ++Lot)
			{
				const FDCVoxelTower& T = Sector.Towers[Lot];
				if (IgnoreLots.Contains(Lot) || (CommittedFallen.IsValidIndex(Lot) && CommittedFallen[Lot]))
				{
					continue;
				}
				const FIntVector L = T.Grid.ToLocal(Cm);
				if (L.X >= T.Min.X && L.Y >= T.Min.Y && L.Z >= 0 && L.X < T.Max.X && L.Y < T.Max.Y && L.Z < T.Max.Z)
				{
					return true;
				}
			}
		}
		return false;
	}
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
	Mid->SetScalarParameterValue(TEXT("FormFloor"), Tn.FormFloor);
	Mid->SetVectorParameterValue(TEXT("FormSky"), Tn.FormSky);
	Mid->SetVectorParameterValue(TEXT("FormGround"), Tn.FormGround);
	Mid->SetVectorParameterValue(TEXT("MoonDir"), FLinearColor(Tn.MoonDir.GetSafeNormal()));
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
	Crumbs = NewObject<UDCCrumbField>(this, TEXT("Crumbs"));
	Crumbs->SetupAttachment(GetRootComponent());
	Crumbs->RegisterComponent();
	TWeakObjectPtr<ADCVoxelCity> Weak(this);
	Crumbs->GroundZ = [Weak](const FVector& P) { return Weak.IsValid() ? Weak->SurfaceZCm(P) : 0.f; };
}

float ADCVoxelCity::BodyLoad() const
{
	return static_cast<float>(BodiesReserved) / FMath::Max(1, UDCVoxelSettings::Get().Tuning.PhysicsMaxBodies);
}

void ADCVoxelCity::ThrowCrumbs(const FFalling& F, int32 FragIndex, const FBox& WorldBox, const FVector& V, int32 Count, FRandomStream& Rng)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (!Crumbs || Count <= 0 || !F.Frags.IsValidIndex(FragIndex) || !WorldBox.IsValid)
	{
		return;
	}
	// Colours come from the fragment's own voxels (a chunk's root is empty: its leaves carry the meshes).
	TArray<const FProcMeshSection*, TInlineAllocator<8>> Sections;
	auto AddSection = [&Sections](const UProceduralMeshComponent* Comp)
	{
		const FProcMeshSection* S = Comp ? const_cast<UProceduralMeshComponent*>(Comp)->GetProcMeshSection(0) : nullptr;
		if (S && S->ProcVertexBuffer.Num() > 0)
		{
			Sections.Add(S);
		}
	};
	const FFalling::FFrag& Fr = F.Frags[FragIndex];
	AddSection(Fr.Comp);
	for (int32 k : Fr.Children)
	{
		AddSection(F.Frags.IsValidIndex(k) ? F.Frags[k].Comp.Get() : nullptr);
	}
	const float GlowScale = Tn.VoxelMaterialGlowScale * Tn.CrumbGlowScale;
	Count = FMath::RoundToInt32(Count * FMath::Clamp(1.f - Tn.CrumbLoadCut * BodyLoad(), 0.f, 1.f)); // thin out under load
	for (int32 i = 0; i < Count; ++i)
	{
		FDCCrumbSpawn C;
		C.At = FVector(Rng.FRandRange(WorldBox.Min.X, WorldBox.Max.X), Rng.FRandRange(WorldBox.Min.Y, WorldBox.Max.Y), Rng.FRandRange(WorldBox.Min.Z, WorldBox.Max.Z));
		const FVector Out = (C.At - WorldBox.GetCenter()).GetSafeNormal2D();
		C.Velocity = V + (Out + Rng.VRand() * 0.6f).GetSafeNormal() * Tn.CrumbSpeedCms * Rng.FRandRange(0.3f, 1.f) + FVector(0, 0, Rng.FRandRange(0.f, Tn.CrumbUpCms));
		C.SizeCm = Tn.CrumbSizeCm * Rng.RandRange(1, FMath::Max(1, Tn.CrumbClusterMax));
		const FProcMeshSection* S = Sections.Num() > 0 ? Sections[Rng.RandRange(0, Sections.Num() - 1)] : nullptr;
		const FColor Pick = S ? S->ProcVertexBuffer[Rng.RandRange(0, S->ProcVertexBuffer.Num() - 1)].Color : FColor(30, 30, 40, 0);
		C.Color = FLinearColor::FromSRGBColor(FColor(Pick.R, Pick.G, Pick.B, 255));
		C.Glow = Pick.A / 255.f * GlowScale;
		Crumbs->Spawn(C);
	}
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

UProceduralMeshComponent* ADCVoxelCity::AcquirePiece(int32 VoxelCm)
{
	TArray<TObjectPtr<UProceduralMeshComponent>>& Free = FreePieces.FindOrAdd(VoxelCm);
	while (Free.Num() > 0)
	{
		UProceduralMeshComponent* Piece = Free.Pop(EAllowShrinking::No);
		if (IsValid(Piece))
		{
			return Piece;
		}
	}
	return NewPiece(VoxelCm);
}

UProceduralMeshComponent* ADCVoxelCity::NewPiece(int32 VoxelCm)
{
	UProceduralMeshComponent* Piece = NewObject<UProceduralMeshComponent>(this);
	Piece->bUseAsyncCooking = true;
	Piece->SetupAttachment(Root);
	Piece->SetCollisionProfileName(TEXT("NoCollision"));
	Piece->SetCastShadow(true);
	Piece->RegisterComponent();
	Piece->SetMaterial(0, VoxelMaterialFor(VoxelCm));
	PieceCm.Add(Piece, VoxelCm);
	PieceAll.Add(Piece);
	return Piece;
}

void ADCVoxelCity::ReleasePiece(UPrimitiveComponent* Piece)
{
	UProceduralMeshComponent* Mesh = Cast<UProceduralMeshComponent>(Piece);
	const int32* Cm = Mesh ? PieceCm.Find(Mesh) : nullptr;
	if (!Cm)
	{
		if (Piece)
		{
			Piece->DestroyComponent(); // not a pooled piece
		}
		return;
	}
	Mesh->ClearAllMeshSections();
	Mesh->SetVisibility(false, false);
	Mesh->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	Mesh->SetRelativeTransform(FTransform::Identity);
	Mesh->SetOwnerNoSee(false);
	Mesh->SetCastShadow(true);
	Mesh->SetMaterial(0, VoxelMaterialFor(*Cm));
	FreePieces.FindOrAdd(*Cm).Add(Mesh);
}

void ADCVoxelCity::PrewarmPieces(int32 PerFrame)
{
	// Enough for the body budget plus the scripted piece roots, created a few per frame (never in a collapse frame).
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const int32 Want = Tn.PhysicsMaxBodies + 16;
	const int32 Cm = FMath::Max(1, Tn.TowerVoxelCm);
	for (int32 n = 0; n < PerFrame && PieceCm.Num() < Want; ++n)
	{
		// A new component each time (acquiring would hand back the one just released: review CP26).
		UProceduralMeshComponent* Piece = NewPiece(Cm);
		Piece->SetVisibility(false, false);
		FreePieces.FindOrAdd(Cm).Add(Piece);
	}
}

void ADCVoxelCity::Clear()
{
	if (Crumbs)
	{
		Crumbs->ClearAll();
	}
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
	for (const TSharedPtr<FFalling>& F : Falling)
	{
		for (FFalling::FFrag& Fr : F->Frags)
		{
			if (Fr.Body)
			{
				ReleaseBox(Fr.Body); // R26 bodies back to the pool
			}
			ReleasePiece(Fr.Comp);
			Fr.Comp = nullptr;
		}
		ReleasePiece(F->Comp);
		F->Comp = nullptr;
	}
	Bodies.Reset();
	BodiesReserved = 0;
	PendingShards.Reset();
	PendingPieces.Reset();
	for (const FDCFading& Fd : Fading)
	{
		if (Fd.Mesh.IsValid())
		{
			ReleasePiece(Fd.Mesh.Get());
		}
		ReleaseBox(Fd.Body.Get());
	}
	Fading.Reset();
	DeadSigns.Reset();
	for (int32 i = 0; i < FireLightPool.Num(); ++i)
	{
		FireLightStart[i] = -1e9f; // out (TickFireLights hides it)
	}
	for (URectLightComponent* L : SignLightPool)
	{
		if (L)
		{
			L->SetVisibility(false);
		}
	}
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

void ADCVoxelCity::Build(int32 RunSeed, int32 SectorIndex, int32 BuildId, bool bAsync)
{
	PendingCraft.Reset(); // a newer build supersedes a craft still in flight (its result is dropped)
	CommittedFallen.Reset();
	HoldRadiusCm = -1.f;
	HoldTops.Reset();
	Clear();
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const UDCTuning& T = UDCTuning::Get();
	const double Start = FPlatformTime::Seconds();
	const FDCCityPlan Plan = PlanSector(RunSeed, SectorIndex);
	LampSpots.Reset();
	for (const FDCPropPlan& Prop : Plan.Props)
	{
		const int32 Row = V.PropLights.IndexOfByPredicate([&Prop](const FDCPropLightDef& D) { return D.Model == Prop.Model; });
		if (Row != INDEX_NONE || (V.PropLights.Num() == 0 && Prop.Model == V.Tuning.LampModel))
		{
			LampSpots.Add({Prop.Location, Row});
		}
	}
	FDCVoxelCraftContext Craft = V.CraftContext(T.City.BlockSizeCm);
	for (const FName& Model : DCVoxelCraft::PlanModels(Plan)) // props and building attachments (R2)
	{
		Craft.Models.Add(Model, DCVoxelModels::Parsed(Model)); // file reads stay on the game thread
	}
	const FDCSeasonDef* Season = SeasonFor(RunSeed, SectorIndex);
	const float SnowCover = Season ? Season->SnowCover : -1.f;
	const int32 Seed = DCCityRules::SectorSeed(RunSeed, SectorIndex);
	const float LotSizeCm = T.City.LotSizeCm;
	// Pure craft from plain copies (plan, context, numbers): the same on the game thread or a worker, so every machine
	// builds the same hash either way.
	auto CraftAll = [Plan, Craft, SnowCover, Seed, LotSizeCm](FDCVoxelSector& Out)
	{
		DCVoxelCraft::CraftCity(Plan, Craft, Out.Towers);
		DCVoxelCraft::CraftTerrain(Out.Terrain, Plan, LotSizeCm, Craft);
		DCVoxelCraft::CraftRubbleZones(Out.Terrain, Plan, Craft);
		DCVoxelCraft::CraftProps(Out.Props, Plan, Craft);
		if (SnowCover >= 0.f)
		{
			LaySnow(Out, SnowCover, Craft.MaterialId(TEXT("Snow")), Seed);
		}
	};
	if (!bAsync)
	{
		CraftAll(Sector);
		FinishBuild(RunSeed, SectorIndex, BuildId, Start);
		return;
	}
	TSharedPtr<FPendingCraft> Pending = MakeShared<FPendingCraft>();
	Pending->Out = MakeShared<FDCVoxelSector>();
	Pending->RunSeed = RunSeed;
	Pending->SectorIndex = SectorIndex;
	Pending->BuildId = BuildId;
	Pending->Start = Start;
	TSharedPtr<FDCVoxelSector> Out = Pending->Out;
	Pending->Pads = MakeShared<TArray<FPrePad>>();
	TSharedPtr<TArray<FPrePad>> Pads = Pending->Pads;
	Pending->HashOut = MakeShared<uint32>(0);
	TSharedPtr<uint32> HashOut = Pending->HashOut;
	Pending->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [CraftAll, Out, Pads, HashOut]()
	{
		CraftAll(*Out);
		// Mesh-job inputs and the content hash, off the game thread too (grid order: terrain, towers, props).
		const int32 N = Out->Towers.Num();
		for (int32 G = 0; G < N + 2; ++G)
		{
			FDCVoxelGrid& Grid = G == 0 ? Out->Terrain : (G <= N ? Out->Towers[G - 1].Grid : Out->Props);
			if (!Grid.Data.HasDirty())
			{
				continue;
			}
			for (const FIntVector& Chunk : Grid.Data.TakeDirty())
			{
				FPrePad& P = Pads->AddDefaulted_GetRef();
				P.Grid = G;
				P.Chunk = Chunk;
				Grid.Data.CopyPadded(Chunk, P.Padded);
			}
		}
		*HashOut = Out->Hash();
	});
	PendingCraft = Pending;
	BuiltId = BuildId; // SyncBuild sees this build as started
}

void ADCVoxelCity::FlushPendingCraft()
{
	if (!PendingCraft.IsValid())
	{
		return;
	}
	// An op that arrives mid-craft (review CP22) waits for the craft here instead of being dropped.
	const TSharedPtr<FPendingCraft> Done = PendingCraft;
	Done->Task.Wait();
	Sector = MoveTemp(*Done->Out);
	FinishBuild(Done->RunSeed, Done->SectorIndex, Done->BuildId, Done->Start, Done->Pads.Get(), *Done->HashOut);
	PendingCraft.Reset();
}

void ADCVoxelCity::FinishBuild(int32 RunSeed, int32 SectorIndex, int32 BuildId, double Start, TArray<FPrePad>* Pads, uint32 Hash)
{
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const UDCTuning& T = UDCTuning::Get();
	OpCtx = V.OpContext(T.City.BlockSizeCm);
	Colors = V.MeshColors();
	BuiltSector = SectorIndex;
	BuiltId = BuildId;
	BuiltSeed = RunSeed;
	AppliedSeq = 0;
	bAsyncLoad = Pads != nullptr;
	if (Pads)
	{
		QueuePadded(*Pads);
	}
	else
	{
		QueueAllDirty(nullptr);
	}
	bLoading = true;
	PrewarmBodies(); // R26 bodies registered once at sector start, parked until a break
	UE_LOG(LogTemp, Display, TEXT("DCVOXEL build sector=%d build=%d towers=%d craft=%.0f ms hash=%08x%s"), SectorIndex, BuildId, Sector.Towers.Num(),
		(FPlatformTime::Seconds() - Start) * 1000.0, Pads ? Hash : Sector.Hash(), Pads ? TEXT(" async") : TEXT(""));
}

FDCVoxelOpResult ADCVoxelCity::ApplyOp(const FDCVoxelOp& Op, bool bAnimate)
{
	FlushPendingCraft();
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
	FDCVoxelOpResult Result = Op.Kind == TEXT("Rest") ? DCVoxelOps::ApplyRest(Sector, Op, PendingFor(Op), OpCtx) : DCVoxelOps::Apply(Sector, Op, OpCtx);
	DCVoxelOps::DeriveCosmetics(Sector, Op, OpCtx, Result);
	AppliedSeq = FMath::Max(AppliedSeq, Op.Seq);
	const uint32 Hash = Sector.Hash();
	FinishOp(Op, Result, bAnimate, static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0), Hash, false);
	return Result;
}

void ADCVoxelCity::ApplyOpAsync(const FDCVoxelOp& Op, bool bAnimate, TFunction<void(const FDCVoxelOpResult&, uint32)> OnDone)
{
	FlushPendingCraft();
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
	FDCPendingPiece Pend = PendingFor(Job->Op);
	InFlightTask = UE::Tasks::Launch(TEXT("DCVoxelOp"), [Job, Target, Ctx, Pend = MoveTemp(Pend)]()
	{
		const double Start = FPlatformTime::Seconds();
		Job->Result = Job->Op.Kind == TEXT("Rest") ? DCVoxelOps::ApplyRest(*Target, Job->Op, Pend, *Ctx) : DCVoxelOps::Apply(*Target, Job->Op, *Ctx);
		const double Applied = FPlatformTime::Seconds();
		DCVoxelOps::DeriveCosmetics(*Target, Job->Op, *Ctx, Job->Result); // shards and fragment trees off the game thread
		const double Derived = FPlatformTime::Seconds();
		Job->Hash = Target->Hash();
		Job->ApplyMs = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
		Job->DeriveMs = static_cast<float>((Derived - Applied) * 1000.0);
		Job->HashMs = static_cast<float>((FPlatformTime::Seconds() - Derived) * 1000.0);
		UE_LOG(LogTemp, Display, TEXT("DCOPTIME seq=%d carve=%.0f structure=%.0f settle=%.0f derive=%.0f hash=%.0f ms"), Job->Op.Seq,
			Job->Result.CarveMs, Job->Result.StructureMs, Job->Result.SettleMs, Job->DeriveMs, Job->HashMs);
	});
}

FDCPendingPiece ADCVoxelCity::PendingFor(const FDCVoxelOp& Op) const
{
	if (Op.Kind != TEXT("Rest"))
	{
		return FDCPendingPiece();
	}
	const TMap<int32, FDCPendingPiece>* M = PendingPieces.Find(Op.RestOf);
	const FDCPendingPiece* P = M ? M->Find(Op.RestPiece) : nullptr;
	return P ? *P : FDCPendingPiece();
}

void ADCVoxelCity::HostRest(FFalling& F)
{
	// R26 host-owned rubble: where the host's bodies came to rest becomes a Rest op every machine applies.
	ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || !GS->HasAuthority() || !bHostRests || GS->Phase != EDCRunPhase::Sector || F.OpSeq <= 0 || F.PieceIndex == INDEX_NONE)
	{
		return;
	}
	FDCVoxelOp Op;
	Op.Kind = TEXT("Rest");
	Op.RestOf = F.OpSeq;
	Op.RestPiece = F.PieceIndex;
	const float SiteRadius = UDCVoxelSettings::Get().Tuning.PhysicsSiteRadiusCm;
	int32 Debris = 0;
	for (int32 f = 0; f < F.Frags.Num(); ++f)
	{
		const UBoxComponent* B = F.Frags[f].Body;
		if (!B)
		{
			continue;
		}
		if (F.Frags[f].State != EFragState::Frozen && FVector::Dist2D(B->GetComponentLocation(), F.LandCenter) > SiteRadius)
		{
			++Debris; // a stray still flying far from the pile is debris, not rubble (advisors CP37): never stamped mid-flight
			continue;
		}
		FDCVoxelRest& R = Op.Rests.AddDefaulted_GetRef();
		R.Frag = f;
		R.PosCm = DCVoxelMath::ToCm(B->GetComponentLocation());
		R.AxisX = DCVoxelOps::AxisIndex(B->GetForwardVector());
		R.AxisZ = DCVoxelOps::AxisIndex(B->GetUpVector());
	}
	if (Op.Rests.Num() == 0 && Debris == 0)
	{
		return;
	}
	// Every body flew off as debris (review CP39): an empty Rest op still goes out, so every machine removes the
	// provisional pile where nothing landed instead of revealing it.
	Op.CenterCm = DCVoxelMath::ToCm(F.LandCenter);
	const int32 Seq = GS->AddVoxelOp(Op).Seq;
	TWeakObjectPtr<ADCGameState> WeakGS(GS);
	UE_LOG(LogTemp, Display, TEXT("DCREST host op=%d rests op %d piece %d: %d bodies, %d flying strays left as debris"), Seq, F.OpSeq, F.PieceIndex, Op.Rests.Num(), Debris);
	ApplyOpAsync(GS->VoxelOps.Last(), true, [WeakGS, Seq](const FDCVoxelOpResult&, uint32 Hash)
	{
		if (ADCGameState* State = WeakGS.Get())
		{
			if (FDCVoxelOp* Rec = State->VoxelOps.FindByPredicate([Seq](const FDCVoxelOp& O) { return O.Seq == Seq; }))
			{
				Rec->ResultHash = Hash;
			}
		}
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
	KillCarvedSigns(Result.TouchedLots);
	if (Op.Kind == TEXT("Strike"))
	{
		const float R2 = FMath::Square(static_cast<float>(FMath::Max(Op.BlastRadiusCm, 0)));
		LampSpots.RemoveAll([&Op, R2](const FLightSpot& L) { return FVector::DistSquared2D(L.At, FVector(Op.CenterCm)) <= R2; }); // lamps in the blast go out
	}
	if (Op.Kind == TEXT("Rest"))
	{
		for (const TSharedPtr<FFalling>& Fl : Falling)
		{
			Fl->bRestNow |= Fl->OpSeq == Op.RestOf && Fl->PieceIndex == Op.RestPiece; // the host's rest decides
		}
		UE_LOG(LogTemp, Display, TEXT("DCREST op=%d rests op %d piece %d: %d bodies, %d rubble cells"), Op.Seq, Op.RestOf, Op.RestPiece, Op.Rests.Num(), Result.Rubble.Num());
	}
	if (bAnimate && Op.Kind == TEXT("Strike") && CollapseCamStep < 0)
	{
		static FString CamDir;
		static const bool bCam = FParse::Value(FCommandLine::Get(), TEXT("dccollapsecam="), CamDir);
		if (bCam)
		{
			CollapseCamStep = 0; // R26 evidence: a fixed wide camera on this collapse (TickCollapseCam)
			CollapseCamOp = Op.Seq;
			CollapseCamCenter = FVector(Op.CenterCm);
			CollapseCamStart = GetWorld()->GetTimeSeconds();
		}
	}
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
	if (bAnimate && Fx && Op.Kind == TEXT("Strike"))
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
	const float Now = StartWorldTime - FallOffset; // falls and rubble reveals run on this machine's presentation clock
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

	const double P0 = FPlatformTime::Seconds();
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
	const double P1 = FPlatformTime::Seconds();
	TArray<FIntVector> DeferList = Defer.Array();
	// R26: the N biggest pieces of this op break into physics bodies at impact (index order breaks ties).
	TArray<int32> BySize;
	for (int32 i = 0; i < Result.Pieces.Num(); ++i)
	{
		BySize.Add(i);
	}
	BySize.StableSort([&Result](int32 A, int32 B) { return Result.Pieces[A].Cells.Num() > Result.Pieces[B].Cells.Num(); });
	TSet<int32> Physical;
	for (const TPair<int32, FDCPendingPiece>& P : Result.Pending)
	{
		Physical.Add(P.Key);
	}
	// Kept for the host's Rest op (R26): derived on the worker (DeriveCosmetics).
	for (TPair<int32, FDCPendingPiece>& P : Result.Pending)
	{
		PendingPieces.FindOrAdd(Op.Seq).Add(P.Key, MoveTemp(P.Value));
	}
	const double P2 = FPlatformTime::Seconds();
	if (bAnimate)
	{
		QueueShards(Op, Result, Now);
	}
	const double P3 = FPlatformTime::Seconds();
	int32 PieceIndex = -1;
	int32 PieceCells = 0;
	for (FDCVoxelPiece& Piece : Result.Pieces)
	{
		PieceCells += Piece.Cells.Num(); // counted before StartFall moves the cells into its mesh tasks
		++PieceIndex;
		if (bAnimate)
		{
			// Where the piece's rubble lies: the landing dust and the decay smoke and fire sit there.
			FVector Sum = FVector::ZeroVector;
			for (int32 i = Piece.RubbleStart; i < Piece.RubbleStart + Piece.RubbleNum; ++i)
			{
				Sum += Sector.Terrain.ToWorld(Result.Rubble[i]);
			}
			const FVector Land = Piece.RubbleNum > 0 ? Sum / Piece.RubbleNum : Sector.Towers[Piece.Lot].Grid.ToWorld(Piece.Pivot);
			StartFall(Piece, Now, DeferList, Land, Physical.Contains(PieceIndex), Op.Seq, PieceIndex);
			DeferList.Reset(); // the first piece to land shows the rubble
		}
	}
	for (const FIntVector& C : DeferList)
	{
		DeferredTerrain.Add({Now, C});
	}
	const double P4 = FPlatformTime::Seconds();
	if ((P4 - P0) * 1000.0 > UDCVoxelSettings::Get().Tuning.HitchLogMs)
	{
		UE_LOG(LogTemp, Display, TEXT("DCPREP op=%d queue=%.1f pend=%.1f shards=%.1f falls=%.1f ms pieces=%d cells=%d jobs=%d"), Op.Seq, (P1 - P0) * 1000.0, (P2 - P1) * 1000.0,
			(P3 - P2) * 1000.0, (P4 - P3) * 1000.0, Result.Pieces.Num(), PieceCells, Jobs.Num());
	}
}

void ADCVoxelCity::StartFall(FDCVoxelPiece& Piece, float Now, const TArray<FIntVector>& Deferred, const FVector& LandAt, bool bPhysics, int32 OpSeq, int32 PieceIndex)
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
	F->Comp = AcquirePiece(Grid.VoxelCm);
	F->Comp->SetWorldLocation(F->Pivot);
	F->Comp->SetVisibility(false);
	// R26 true break-up: chunks (PhysicsFragmentCm blocks) that break off mid-fall, each made of leaves
	// (PhysicsLeafCm blocks) that a hard impact shatters it into. Over the body budget: the piece falls scripted.
	// The piece's fragment tree was kept with its op (PendingPieces): the same tree the host's Rest op names.
	const FDCPendingPiece* Pend = nullptr;
	if (bPhysics)
	{
		const TMap<int32, FDCPendingPiece>* M = PendingPieces.Find(OpSeq);
		Pend = M ? M->Find(PieceIndex) : nullptr;
	}
	int32 NumBodies = Pend ? Pend->FragParent.Num() : 0;
	if (!Pend || NumBodies == 0 || BodiesReserved + NumBodies > Tn.PhysicsMaxBodies)
	{
		Pend = nullptr; // over the body budget: this piece falls scripted
		NumBodies = 0;
	}
	F->OpSeq = OpSeq;
	{
		const FDCVoxelTower& T = Sector.Towers[Piece.Lot];
		F->LotBox = FBox2D(FVector2D(T.FootMinCm().X, T.FootMinCm().Y), FVector2D(T.FootMaxCm().X, T.FootMaxCm().Y));
	}
	F->PieceIndex = PieceIndex;
	F->Seed = DCVoxelOps::Hash(Piece.Pivot, static_cast<uint32>(Piece.Lot) * 7919u + static_cast<uint32>(Piece.Cells.Num()));
	Falling.Add(F);
	for (const FIntVector& C : Deferred)
	{
		if (Pend)
		{
			F->RevealOnRest.Add(C); // a body's rubble shows when it comes to rest
			continue;
		}
		DeferredTerrain.Add({Now + F->Duration, C});
	}

	// Mesh the piece off the game thread (cells relative to the hinge, so rotating the component tips it over). A
	// physics piece meshes each fragment into its own component riding on the (empty) piece root.
	const float Vcm = Grid.VoxelCm;
	const FIntVector Pivot = Piece.Pivot;
	auto LaunchJob = [this, &F, Vcm, Pivot](TArray<FIntVector> PieceCells, TArray<uint8> Mats, int32 Frag)
	{
		TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
		Job->Grid = INDEX_NONE;
		Job->BuildId = BuiltId;
		Job->Piece = F;
		Job->Frag = Frag;
		TArray<FColor> PieceColors = Colors;
		FDCVoxelMeshData* Out = &Job->Mesh;
		Job->Task = UE::Tasks::Launch(TEXT("DCVoxelPieceMesh"), [PieceCells = MoveTemp(PieceCells), Mats = MoveTemp(Mats), Pivot, PieceColors = MoveTemp(PieceColors), Vcm, Out]()
		{
			DCVoxelMesher::MeshCells(PieceCells, Mats, Pivot, PieceColors, Vcm, *Out);
		});
		Jobs.Add(Job);
	};
	if (!Pend)
	{
		F->PendingMeshes = 1;
		LaunchJob(MoveTemp(Piece.Cells), MoveTemp(Piece.Materials), INDEX_NONE); // nothing reads them after the fall starts
	}
	else
	{
		BodiesReserved += NumBodies;
		const FVector PivotWorld = Grid.ToWorld(Piece.Pivot);
		// One copy of the piece's cells shared by every fragment's mesh task; each task gathers its own cells (the
		// game thread only takes integer bounds: R13 collapse frames, advisor CP26).
		const TSharedRef<const TArray<FIntVector>> AllCells = MakeShared<const TArray<FIntVector>>(MoveTemp(Piece.Cells));
		const TSharedRef<const TArray<uint8>> AllMats = MakeShared<const TArray<uint8>>(MoveTemp(Piece.Materials));
		for (int32 f = 0; f < Pend->FragParent.Num(); ++f)
		{
			const TArray<int32>& Idx = Pend->FragCells[f];
			const int32 Parent = Pend->FragParent[f];
			FFalling::FFrag& Fr = F->Frags.AddDefaulted_GetRef();
			Fr.Parent = Parent;
			Fr.Comp = AcquirePiece(Grid.VoxelCm); // a chunk's stays empty: its leaves ride on it
			Fr.Comp->AttachToComponent(Parent == INDEX_NONE ? F->Comp.Get() : F->Frags[Parent].Comp.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			if (Idx.Num() > 0 && Pend->FragLo.IsValidIndex(f) && Pend->FragHi.IsValidIndex(f))
			{
				const FIntVector& Lo = Pend->FragLo[f]; // bounds derived on the worker (DeriveCosmetics)
				const FIntVector& Hi = Pend->FragHi[f];
				Fr.Local = FBox(Grid.ToWorld(Lo) - PivotWorld, Grid.ToWorld(Hi + FIntVector(1)) - PivotWorld); // ToWorld is affine
			}
			Fr.Mass = FMath::Max(static_cast<float>(Idx.Num()) * Vcm * Vcm * Vcm * Tn.PhysicsDensity, 5.f);
			if (Parent == INDEX_NONE)
			{
				continue;
			}
			F->Frags[Parent].Children.Add(f);
			Fr.Comp->SetVisibility(false);
			++F->PendingMeshes;
			TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
			Job->Grid = INDEX_NONE;
			Job->BuildId = BuiltId;
			Job->Piece = F;
			Job->Frag = f;
			FDCVoxelMeshData* Out = &Job->Mesh;
			Job->Task = UE::Tasks::Launch(TEXT("DCVoxelPieceMesh"), [AllCells, AllMats, Idx, Pivot, PieceColors = Colors, Vcm, Out]()
			{
				TArray<FIntVector> FragCells;
				TArray<uint8> FragMats;
				FragCells.Reserve(Idx.Num());
				FragMats.Reserve(Idx.Num());
				for (int32 i : Idx)
				{
					FragCells.Add((*AllCells)[i]);
					FragMats.Add(AllMats->IsValidIndex(i) ? (*AllMats)[i] : 1);
				}
				DCVoxelMesher::MeshCells(FragCells, FragMats, Pivot, PieceColors, Vcm, *Out);
			});
			Jobs.Add(Job);
		}
	}
	DCAudio::PlayAt(this, TEXT("Creak"), F->Pivot, 1.f);
}

void ADCVoxelCity::QueueShards(const FDCVoxelOp& Op, const FDCVoxelOpResult& Result, float Start)
{
	// R26 shatter: the voxels the strike blew out of the towers fly as small bodies, single voxels near the blast
	// centre and clusters of a few voxels further out (cluster edge grows by ShardGrowCm), capped and sampled by a
	// seeded hash; they keep their palette colour and glow (lit windows stay lit in the air). Cosmetic and local.
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (Result.ShardClusters.Num() == 0)
	{
		return;
	}
	const FVector Center(Op.CenterCm);
	FDCShardBurst& Burst = PendingShards.AddDefaulted_GetRef();
	Burst.Start = Start;
	const float GlowScale = Tn.VoxelMaterialGlowScale;
	FRandomStream Rng(static_cast<int32>(DCVoxelOps::Hash(Op.CenterCm, static_cast<uint32>(Op.Seq))));
	FRandomStream CrumbRng(static_cast<int32>(DCVoxelOps::Hash(Op.CenterCm, static_cast<uint32>(Op.Seq) ^ 0xC0B5u)));
	for (const FDCShardCluster& C : Result.ShardClusters)
	{
		const FVector P = C.Pos;
		const FVector Out = (P - Center).GetSafeNormal();
		const float EdgeCm = C.VoxelCm * FMath::Max(1.f, FMath::Pow(static_cast<float>(C.Count), 1.f / 3.f));
		FDCDebrisSpawn D;
		D.Transform = FTransform(FRotator(Rng.FRandRange(0, 360), Rng.FRandRange(0, 360), 0), P, FVector(EdgeCm / 100.f));
		const float Speed = Tn.ShardSpeedCms * Rng.FRandRange(0.5f, 1.1f) / FMath::Sqrt(static_cast<float>(C.Block));
		D.LinearVelocity = (Out + Rng.VRand() * 0.35f).GetSafeNormal() * Speed + FVector(0, 0, Rng.FRandRange(0.f, Tn.ShardUpCms));
		D.AngularVelocityDeg = Rng.VRand() * 720.f;
		// Lit voxels (windows, neon) keep their own colour and glow in the air (R26: emissive faces while falling);
		// dull wall voxels fly as their rubble colour (critic: not window-coloured confetti).
		const bool bLit = Colors.IsValidIndex(C.Material) && Colors[C.Material].A > 0;
		const uint8 Rub = !bLit && OpCtx.RubbleOf.IsValidIndex(C.Material) && OpCtx.RubbleOf[C.Material] != 0 ? OpCtx.RubbleOf[C.Material] : C.Material;
		const FColor Col = Colors.IsValidIndex(Rub) ? Colors[Rub] : FColor(30, 30, 40, 0);
		const bool bEmber = Rng.RandRange(0, 99) < Tn.ShardEmberPercent && !bLit; // embers only from dull voxels
		D.Color = bEmber ? Tn.ShardEmberColor : FLinearColor::FromSRGBColor(FColor(Col.R, Col.G, Col.B, 255));
		D.Glow = bEmber ? Tn.ShardEmberGlow : Col.A / 255.f * GlowScale * Tn.ShardGlowScale;
		D.LifeSeconds = Tn.ShardLifeSeconds * Rng.FRandRange(0.7f, 1.2f);
		Burst.Spawns.Add(D);
		// Crumbs: single voxels around each cluster, so the blast throws grit, not only blocks.
		for (int32 c = 0; c < Tn.CrumbsPerShardCluster; ++c)
		{
			FDCCrumbSpawn& Cr = Burst.Crumbs.AddDefaulted_GetRef();
			Cr.At = P + CrumbRng.VRand() * EdgeCm;
			Cr.Velocity = (Out + CrumbRng.VRand() * 0.6f).GetSafeNormal() * Tn.ShardSpeedCms * CrumbRng.FRandRange(0.4f, 1.2f) + FVector(0, 0, CrumbRng.FRandRange(0.f, Tn.ShardUpCms));
			Cr.SizeCm = C.VoxelCm * CrumbRng.RandRange(1, FMath::Max(1, Tn.CrumbClusterMax));
			Cr.Color = D.Color;
			Cr.Glow = bEmber ? D.Glow : D.Glow * Tn.CrumbGlowScale / FMath::Max(Tn.ShardGlowScale, 0.01f);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DCPHYS shards op=%d clusters=%d (cap %d)"), Op.Seq, Burst.Spawns.Num(), Tn.ShardMax);
}

void ADCVoxelCity::SpawnDueShards(float Now)
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr;
	for (int32 i = PendingShards.Num() - 1; i >= 0; --i)
	{
		if (Now < PendingShards[i].Start)
		{
			continue;
		}
		for (const FDCDebrisSpawn& D : PendingShards[i].Spawns)
		{
			if (Pool)
			{
				Pool->Spawn(D);
			}
		}
		for (const FDCCrumbSpawn& C : PendingShards[i].Crumbs)
		{
			if (Crumbs)
			{
				Crumbs->Spawn(C);
			}
		}
		PendingShards.RemoveAtSwap(i);
	}
}

void ADCVoxelCity::QueuePadded(TArray<FPrePad>& Pads)
{
	const float AO = UDCVoxelSettings::Get().Tuning.AOStrength;
	for (FPrePad& P : Pads)
	{
		TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
		Job->Grid = P.Grid;
		Job->Chunk = P.Chunk;
		Job->BuildId = BuiltId;
		FDCVoxelMeshData* Out = &Job->Mesh;
		TArray<FColor> JobColors = Colors;
		const float Vcm = GridAt(P.Grid).VoxelCm;
		Job->Task = UE::Tasks::Launch(TEXT("DCVoxelChunkMesh"), [Padded = MoveTemp(P.Padded), JobColors = MoveTemp(JobColors), Vcm, AO, Out]()
		{
			DCVoxelMesher::MeshPadded(Padded, JobColors, Vcm, AO, *Out);
		});
		Jobs.Add(Job);
	}
	Pads.Reset();
}

void ADCVoxelCity::QueueDirty(int32 GridIndex, const TSet<FIntVector>& Chunks)
{
	const FDCVoxelGrid& Grid = GridAt(GridIndex);
	const float Vcm = Grid.VoxelCm;
	const float AO = UDCVoxelSettings::Get().Tuning.AOStrength;
	// Snapshots in parallel (read-only on the grid; a whole city at once cost ~0.7 s on one thread: reel restage).
	TArray<FIntVector> List = Chunks.Array();
	TArray<TArray<uint8>> Padded;
	Padded.SetNum(List.Num());
	ParallelFor(List.Num(), [&](int32 i) { Grid.Data.CopyPadded(List[i], Padded[i]); }, List.Num() < 16);
	for (int32 i = 0; i < List.Num(); ++i)
	{
		TSharedPtr<FMeshJob> Job = MakeShared<FMeshJob>();
		Job->Grid = GridIndex;
		Job->Chunk = List[i];
		Job->BuildId = BuiltId;
		FDCVoxelMeshData* Out = &Job->Mesh;
		TArray<FColor> JobColors = Colors;
		Job->Task = UE::Tasks::Launch(TEXT("DCVoxelChunkMesh"), [Pad = MoveTemp(Padded[i]), JobColors = MoveTemp(JobColors), Vcm, AO, Out]()
		{
			DCVoxelMesher::MeshPadded(Pad, JobColors, Vcm, AO, *Out);
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
	const double Start = FPlatformTime::Seconds();
	const double PieceBudgetS = UDCVoxelSettings::Get().Tuning.PieceUploadBudgetMs / 1000.0;
	for (int32 i = 0; i < Jobs.Num();)
	{
		const TSharedPtr<FMeshJob> Job = Jobs[i];
		if (!Job->Task.IsCompleted() || (Job->Piece && FPlatformTime::Seconds() - Start > PieceBudgetS))
		{
			++i; // not done, or a piece section over this frame's budget: next frame
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
			UProceduralMeshComponent* Target = F.Frags.IsValidIndex(Job->Frag) ? F.Frags[Job->Frag].Comp.Get() : F.Comp.Get();
			if (Target && F.Comp && !F.bLanded)
			{
				Target->CreateMeshSection(0, Job->Mesh.Positions, Job->Mesh.Indices, Job->Mesh.Normals, Job->Mesh.UVs, Job->Mesh.Colors, {}, false);
				if (--F.PendingMeshes <= 0)
				{
					F.Comp->SetVisibility(GetWorld()->GetTimeSeconds() - FallOffset >= F.Start, true); // a piece meshed ahead shows at the detonation
					F.bReady = true;
				}
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
	// Load cap in cells too: the render thread pays per new section, not per game-thread ms (perf-r13m load spikes).
	// Only under the sector card: a reel build (negative id, synchronous or behind the reel slate) uploads at the old
	// load pace so a clip never plays in a half-uploaded city (review CP29).
	const bool bCardLoad = bLoading && bAsyncLoad && BuiltId >= 0;
	const int32 Cap = bLoading ? (bCardLoad ? FMath::Max(1, Tn.UploadLoadMaxCells) : MAX_int32) : FMath::Max(1, Tn.UploadsPerFrame);
	const float BudgetMs = bLoading ? (bCardLoad ? Tn.UploadLoadBudgetMs : Tn.UploadSyncLoadBudgetMs) : Tn.UploadBudgetMs;
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
	SpawnDueShards(Now);
	// Rested bodies shrink away over the shared rubble, then go.
	const float Fade = FMath::Max(UDCVoxelSettings::Get().Tuning.PhysicsFadeSeconds, 0.01f);
	for (int32 i = Fading.Num() - 1; i >= 0; --i)
	{
		const float A = (GetWorld()->GetTimeSeconds() - Fading[i].Start) / Fade;
		UPrimitiveComponent* Mesh = Fading[i].Mesh.Get();
		if (A >= 1.f || !Mesh)
		{
			if (Mesh)
			{
				ReleasePiece(Mesh);
			}
			ReleaseBox(Fading[i].Body.Get());
			BodiesReserved = FMath::Max(0, BodiesReserved - (Fading[i].bReserved ? 1 : 0));
			Fading.RemoveAtSwap(i);
			continue;
		}
		if (Fading[i].Body.IsValid())
		{
			Mesh->SetRelativeScale3D(FVector(FMath::Max(1.f - A, 0.01f))); // leaves shrink with their chunk
		}
	}
	for (int32 i = Falling.Num() - 1; i >= 0; --i)
	{
		FFalling& F = *Falling[i];
		const float Age = FMath::Max(Now - F.Start, 0.f);
		const float U = FMath::Clamp(Age / F.Duration, 0.f, 1.f);
		if (F.Comp && F.bReady && Now >= F.Start && !F.Comp->IsVisible())
		{
			F.Comp->SetVisibility(true, true);
		}
		if (F.bBroken)
		{
			if (F.bRestNow || TickBodies(Falling[i], GetWorld()->GetDeltaSeconds() * UDCPresentationClock::Dilation(this)))
			{
				if (!F.bRestNow)
				{
					HostRest(F); // host only: its rest poses become the shared rubble
				}
				Land(F);
				Falling.RemoveAtSwap(i);
			}
			continue;
		}
		const float Drop = FMath::Min(0.5f * UDCVoxelSettings::Get().Tuning.FallGravityCm * Age * Age, F.DropCm);
		if (F.Comp)
		{
			// Drops from the first frame under (exaggerated) gravity, so the top never hangs over the bitten floors,
			// and leans away from the blast as it goes.
			F.Comp->SetWorldRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.MaxAngle * U * U)));
			F.Comp->SetWorldLocation(F.Pivot - FVector(0, 0, Drop));
		}
		if (F.Frags.Num() > 0)
		{
			const bool bImpact = Drop >= F.DropCm || U >= 1.f || LowestFragmentAboveGround(F) <= 0.f;
			if (F.bReady && Now >= F.Start && (bImpact || U >= UDCVoxelSettings::Get().Tuning.PhysicsBreakAtU))
			{
				BreakApart(Falling[i], Age, U, bImpact); // chunks break off as it falls (or at the impact)
			}
			continue;
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

float ADCVoxelCity::BodyGroundZ(const FVector& At, const FVector& LandCenter) const
{
	// The street under the body: the terrain top, but never the op's own rubble (written to the grid at apply, its
	// mesh and collision only show when the bodies rest).
	return FMath::Min(SurfaceZCm(At), LandCenter.Z);
}

float ADCVoxelCity::LowestFragmentAboveGround(const FFalling& F) const
{
	const FTransform Xf = F.Comp->GetComponentTransform();
	float Lowest = TNumericLimits<float>::Max();
	for (const FFalling::FFrag& Fr : F.Frags)
	{
		if (!Fr.Local.IsValid || Fr.Parent != INDEX_NONE)
		{
			continue;
		}
		const FBox W = Fr.Local.TransformBy(Xf);
		Lowest = FMath::Min(Lowest, W.Min.Z - BodyGroundZ(W.GetCenter(), F.LandCenter));
	}
	return Lowest;
}

UBoxComponent* ADCVoxelCity::AcquireBox()
{
	if (BoxFree.Num() > 0)
	{
		return BoxFree.Pop(EAllowShrinking::No);
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(this);
	Box->SetCollisionProfileName(TEXT("PhysicsActor"));
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // pieces never shove players around
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore); // photos and rifle traces see the shared state
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetHiddenInGame(true);
	Box->RegisterComponent();
	Box->OnComponentHit.AddDynamic(this, &ADCVoxelCity::OnBodyHit);
	BoxAll.Add(Box);
	return Box;
}

void ADCVoxelCity::ReleaseBox(UPrimitiveComponent* Body)
{
	UBoxComponent* Box = Cast<UBoxComponent>(Body);
	if (!Box)
	{
		return;
	}
	Box->SetSimulatePhysics(false);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetNotifyRigidBodyCollision(false);
	BoxFree.AddUnique(Box);
}

void ADCVoxelCity::PrewarmBodies()
{
	const int32 Want = UDCVoxelSettings::Get().Tuning.PhysicsMaxBodies;
	TArray<UBoxComponent*> Made;
	while (BoxAll.Num() < Want)
	{
		Made.Add(AcquireBox());
	}
	for (UBoxComponent* B : Made)
	{
		BoxFree.Add(B);
	}
}

UBoxComponent* ADCVoxelCity::MakeBody(const TSharedPtr<FFalling>& Shared, int32 Index, const FVector& At, const FQuat& Rot, const FVector& V, const FVector& SpinDeg, bool bSolid)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	FFalling::FFrag& Fr = Shared->Frags[Index];
	UBoxComponent* Box = AcquireBox(); // pooled: a break never allocates or registers components (advisor CP10)
	Box->SetWorldLocationAndRotation(At, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	Box->SetBoxExtent(Fr.Local.GetExtent() * Tn.PhysicsBoxScale);
	// A ghost to the city until it is down near its rubble site: chunks that break off beside their own stump fall
	// clear of it (the tower's async-cooked collision also lags the carve). Bodies collide with each other at once.
	Box->SetCollisionResponseToChannel(ECC_WorldStatic, bSolid ? ECR_Block : ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	const bool bChunk = Fr.Children.Num() > 1;
	Box->SetNotifyRigidBodyCollision(bChunk); // hits matter to chunks (they shatter); leaves skip the callback cost
	Box->SetUseCCD(bChunk); // fast big boxes over a thin street mesh; leaves are slower and many
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetSimulatePhysics(true);
	Box->SetLinearDamping(Tn.PhysicsLinearDamping);
	Box->SetAngularDamping(Tn.PhysicsAngularDamping);
	Box->SetMassOverrideInKg(NAME_None, Fr.Mass, true);
	Fr.Comp->AttachToComponent(Box, FAttachmentTransformRules::KeepWorldTransform); // keeps its emissive faces
	Box->SetPhysicsLinearVelocity(V);
	Box->SetPhysicsAngularVelocityInDegrees(SpinDeg);
	Fr.Body = Box;
	Fr.State = bSolid ? EFragState::Solid : EFragState::Ghost;
	{
		// Own deadline from its own release height (advisor CP27 #3): free fall to the site plus settle, capped.
		const float H = FMath::Max(At.Z - Shared->LandCenter.Z, 0.f);
		Fr.Deadline = Shared->BodyAge + FMath::Min(FMath::Sqrt(2.f * H / 980.f) + Tn.PhysicsSettleSeconds, Tn.PhysicsMaxAirSeconds);
	}
	Bodies.Add(Box, Shared);
	return Box;
}

void ADCVoxelCity::BreakApart(const TSharedPtr<FFalling>& Shared, float Age, float U, bool bImpact)
{
	FFalling& F = *Shared;
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	F.bBroken = true;
	F.BodyAge = 0.f;
	const FTransform Xf = F.Comp->GetComponentTransform();
	// Speed now: the fall (most of it lost if this is the impact), the topple's swing (omega x r), scatter and a kick.
	const float FallCms = FMath::Min(Tn.FallGravityCm * Age, FMath::Sqrt(2.f * Tn.FallGravityCm * FMath::Max(F.DropCm, 1.f)));
	const float Keep = bImpact ? Tn.PhysicsKeepFallSpeed : 1.f;
	const float SwingRad = U < 1.f ? FMath::DegreesToRadians(F.MaxAngle * 2.f * U / FMath::Max(F.Duration, 0.05f)) : 0.f;
	FBox All(ForceInit);
	for (const FFalling::FFrag& Fr : F.Frags)
	{
		All += Fr.Local;
	}
	const FVector Center = Xf.TransformPosition(All.GetCenter());
	FRandomStream Rng(static_cast<int32>(F.Seed));
	FRandomStream CrumbRng(static_cast<int32>(F.Seed ^ 0xC0B5u)); // own stream: crumbs never shift the bodies' draws
	int32 Made = 0;
	for (int32 g = 0; g < F.Frags.Num(); ++g)
	{
		FFalling::FFrag& Fr = F.Frags[g];
		if (Fr.Parent != INDEX_NONE || !Fr.Comp || !Fr.Local.IsValid)
		{
			continue;
		}
		FVector At = Xf.TransformPosition(Fr.Local.GetCenter());
		// Never start under the street (a body inside the street mesh falls through it): lift it to stand on it.
		const FBox W = Fr.Local.TransformBy(Xf);
		const float Lift = FMath::Max(0.f, BodyGroundZ(At, F.LandCenter) + 5.f - (W.Min.Z + W.GetExtent().Z * (1.f - Tn.PhysicsBoxScale)));
		At.Z += Lift;
		Fr.Comp->SetWorldLocation(Fr.Comp->GetComponentLocation() + FVector(0, 0, Lift));
		const FVector Swing = FVector::CrossProduct(F.Axis * SwingRad, At - Xf.GetLocation());
		FVector Out = At - Center;
		Out.Z *= 0.3f;
		const FVector V = FVector(0, 0, -FallCms * Keep + (bImpact ? Tn.PhysicsKickUpCms : 0.f)) + Swing * Keep
			+ Out.GetSafeNormal() * Tn.PhysicsScatterCms * Rng.FRandRange(0.5f, 1.2f);
		MakeBody(Shared, g, At, Xf.GetRotation(), V, Rng.VRand() * Tn.PhysicsSpinDegS + F.Axis * FMath::RadiansToDegrees(SwingRad), false);
		ThrowCrumbs(F, g, W.ShiftBy(FVector(0, 0, Lift)), V, Tn.CrumbsPerChunk, CrumbRng); // R26: the break throws crumbs, not only chunks
		++Made;
	}
	if (Fx && bImpact)
	{
		Fx->Emit(TEXT("Landing"), F.LandCenter + FVector(0, 0, 100.f), Tn.TerrainVoxelCm * 10.f);
	}
	DCAudio::PlayAt(this, TEXT("Collapse"), Center, bImpact ? 1.f : 0.7f);
	UE_LOG(LogTemp, Display, TEXT("DCPHYS break %d chunks (%d bodies reserved) at z=%.0f (rubble site z=%.0f) %s, fall %.0f cm/s swing %.2f rad/s"), Made, BodiesReserved, Center.Z, F.LandCenter.Z,
		bImpact ? TEXT("at the impact") : TEXT("mid-fall"), FallCms, SwingRad);
}

void ADCVoxelCity::Shatter(const TSharedPtr<FFalling>& Shared, int32 ChunkIndex)
{
	FFalling& F = *Shared;
	FFalling::FFrag& Chunk = F.Frags[ChunkIndex];
	if (!Chunk.Body || Chunk.bSplit)
	{
		return;
	}
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	// The chunk's leaves become bodies where they sit, moving as the chunk moved (v + omega x r) plus a burst.
	const FTransform B = Chunk.Body->GetComponentTransform();
	const FVector V = Chunk.Body->GetPhysicsLinearVelocity();
	const FVector W = FMath::DegreesToRadians(1.f) * Chunk.Body->GetPhysicsAngularVelocityInDegrees();
	const bool bSolid = Chunk.IsSolid();
	Bodies.Remove(Chunk.Body.Get());
	UBoxComponent* Old = Chunk.Body;
	Chunk.Body = nullptr;
	Chunk.bSplit = true;
	FRandomStream Rng(static_cast<int32>(F.Seed + ChunkIndex * 977u));
	const TArray<int32> Kids = Chunk.Children;
	const FVector ChunkCenter = Chunk.Local.GetCenter();
	for (int32 k : Kids)
	{
		const FFalling::FFrag& Leaf = F.Frags[k];
		const FVector At = B.TransformPosition(Leaf.Local.GetCenter() - ChunkCenter);
		const FVector R = At - B.GetLocation();
		const FVector LeafV = V + FVector::CrossProduct(W, R) + R.GetSafeNormal() * Tn.PhysicsShatterBurstCms * Rng.FRandRange(0.4f, 1.f) + FVector(0, 0, Tn.PhysicsShatterBurstCms * 0.5f);
		MakeBody(Shared, k, At, B.GetRotation(), LeafV, Rng.VRand() * Tn.PhysicsSpinDegS * 1.5f, bSolid);
	}
	FRandomStream CrumbRng(static_cast<int32>(F.Seed + ChunkIndex * 977u) ^ 0xC0B5);
	ThrowCrumbs(F, ChunkIndex, FBox::BuildAABB(B.GetLocation(), Chunk.Local.GetExtent()), V * 0.5f, Tn.CrumbsPerShatter, CrumbRng);
	if (Chunk.Comp)
	{
		Chunk.Comp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	ReleaseBox(Old);
	UE_LOG(LogTemp, Display, TEXT("DCPHYS shatter chunk into %d pieces at z=%.0f (%.0f cm/s)"), Kids.Num(), B.GetLocation().Z, V.Size());
}

bool ADCVoxelCity::TickBodies(const TSharedPtr<FFalling>& Shared, float Dt)
{
	FFalling& F = *Shared;
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	F.BodyAge += Dt;
	for (int32 i = 0; i < F.Frags.Num(); ++i)
	{
		if (F.Frags[i].bSplitPending && !F.Frags[i].bSplit)
		{
			F.Frags[i].bSplitPending = false;
			Shatter(Shared, i); // a hard hit last frame: break it here, never inside the physics callback
		}
	}
	// A piece rests when every body is frozen or past its own deadline (advisor CP27): no single budget for all.
	bool bAllDone = true;
	F.Late = 0;
	F.Stray = 0;
	for (FFalling::FFrag& Fr : F.Frags)
	{
		if (!Fr.Body || Fr.State == EFragState::Frozen)
		{
			continue;
		}
		const FBox B = Fr.Body->Bounds.GetBox();
		const float GroundZ = BodyGroundZ(B.GetCenter(), F.LandCenter);
		if (B.Max.Z < GroundZ - 100.f)
		{
			Fr.Body->PutRigidBodyToSleep(); // slipped under the street: it rests (hidden by the rubble swap)
			Fr.Body->SetSimulatePhysics(false);
			Fr.State = EFragState::Frozen;
			continue;
		}
		// Solid once near its rubble site's height, or once clear of the struck lot (then it never passes through the
		// standing towers it flies into).
		const FBox2D Clear = F.LotBox.ExpandBy(Tn.PhysicsLotMarginCm);
		const bool bClearOfLot = F.LotBox.bIsValid && !Clear.Intersect(FBox2D(FVector2D(B.Min), FVector2D(B.Max)));
		if (Fr.State == EFragState::Ghost && (B.Min.Z <= F.LandCenter.Z + Tn.PhysicsSolidAboveSiteCm || bClearOfLot))
		{
			Fr.State = EFragState::Solid;
			Fr.Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		}
		if (Fr.State == EFragState::Solid && B.Min.Z <= GroundZ + Tn.PhysicsGroundContactCm)
		{
			Fr.State = EFragState::Grounded; // on the street: settle damping (a fall stays free, a pile stops jittering)
			Fr.Body->SetLinearDamping(Tn.PhysicsGroundLinearDamping);
			Fr.Body->SetAngularDamping(Tn.PhysicsGroundAngularDamping);
		}
		const FVector Vel = Fr.Body->GetPhysicsLinearVelocity();
		if (Tn.PhysicsSteer > 0.f && F.BodyAge < Tn.PhysicsSteerSeconds) // optional pull toward the plan-time site
		{
			const FVector At = Fr.Body->GetComponentLocation();
			const FVector Off(F.LandCenter.X - At.X, F.LandCenter.Y - At.Y, 0.f);
			Fr.Body->AddForce(Off * Tn.PhysicsSteer - FVector(Vel.X, Vel.Y, 0.f) * (2.f * FMath::Sqrt(Tn.PhysicsSteer)), NAME_None, true);
		}
		// Each body freezes on its own: asleep at once from any state (asleep on a frozen pile is at rest), or slow
		// for RestSeconds once solid (a touching pile is one island that may never sleep).
		const bool bAsleep = !Fr.Body->IsAnyRigidBodyAwake();
		const bool bSlow = bAsleep || (Vel.Size() < Tn.PhysicsRestCms && Fr.Body->GetPhysicsAngularVelocityInDegrees().Size() < Tn.PhysicsRestDegS);
		Fr.StillFor = bSlow ? Fr.StillFor + Dt : 0.f;
		if (bAsleep || (Fr.IsSolid() && Fr.StillFor >= Tn.PhysicsRestSeconds))
		{
			Fr.State = EFragState::Frozen;
			Fr.Body->SetSimulatePhysics(false); // kinematic where it lies; others still land on it
			continue;
		}
		if (FVector::Dist2D(B.GetCenter(), F.LandCenter) > Tn.PhysicsSiteRadiusCm)
		{
			++F.Stray; // flung far from the pile (advisor CP27 #4): it keeps flying and does not hold the rest
			continue;
		}
		if (F.BodyAge >= Fr.Deadline)
		{
			++F.Late; // past its own deadline: it no longer holds the piece up (Land sets it down if still in the air)
			continue;
		}
		bAllDone = false;
	}
	F.StillFor = F.Late == 0 && bAllDone ? F.StillFor + Dt : 0.f;
	return (F.BodyAge > 0.5f && bAllDone) || F.BodyAge >= Tn.PhysicsMaxAirSeconds + Tn.PhysicsSettleSeconds;
}

void ADCVoxelCity::OnBodyHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const TWeakPtr<FFalling>* Found = Bodies.Find(HitComp);
	const TSharedPtr<FFalling> F = Found ? Found->Pin() : nullptr;
	FFalling::FFrag* Fr = F ? F->Frags.FindByPredicate([HitComp](const FFalling::FFrag& X) { return X.Body == HitComp; }) : nullptr;
	if (!Fr)
	{
		return;
	}
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const float DeltaV = NormalImpulse.Size() / FMath::Max(Fr->Mass, 1.f); // cm/s of speed lost in this hit
	if (Fr->Children.Num() > 1 && !Fr->bSplit && DeltaV >= Tn.PhysicsShatterCms && F->BodyAge >= Tn.PhysicsShatterGraceSeconds)
	{
		Fr->bSplitPending = true; // breaks again on the impact (next tick)
	}
	if (DeltaV < Tn.PhysicsBreakCms || F->BodyAge - Fr->LastChip < Tn.PhysicsBreakCooldown)
	{
		return;
	}
	Fr->LastChip = F->BodyAge;
	// A hard impact chips the piece: dust and a burst of its own coloured voxels fly off (cosmetic, local).
	const FVector Where = Hit.ImpactPoint;
	if (Fx)
	{
		Fx->Emit(TEXT("Landing"), Where, Sector.Terrain.VoxelCm * 6.f);
	}
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr)
	{
		FRandomStream Rng(GetTypeHash(Where));
		for (int32 i = 0; i < Tn.LandingDebris / 3; ++i)
		{
			FDCDebrisSpawn D;
			D.Transform = FTransform(FRotator(Rng.FRandRange(0, 360), Rng.FRandRange(0, 360), 0), Where + Rng.VRand() * 80.f, FVector(Rng.FRandRange(0.06f, 0.2f)));
			D.LinearVelocity = Rng.VRand() * 500.f + FVector(0, 0, 400.f);
			D.AngularVelocityDeg = Rng.VRand() * 400.f;
			D.Color = F->Color * Rng.FRandRange(0.8f, 1.4f);
			D.LifeSeconds = UDCTuning::Get().City.DebrisLifeSeconds * 0.5f;
			Pool->Spawn(D);
		}
	}
	DCAudio::PlayAt(this, TEXT("Collapse"), Where, 0.6f);
}

void ADCVoxelCity::Land(FFalling& F)
{
	F.bLanded = true;
	if (F.Frags.Num() > 0)
	{
		// The bodies rest: the shared rubble shows under the landing dust and the fragments go.
		float Spread = 0.f;
		int32 Awake = 0;
		int32 StillReserved = 0;
		TBitArray<> FadingFrag(false, F.Frags.Num());
		for (int32 f = 0; f < F.Frags.Num(); ++f)
		{
			FFalling::FFrag& Fr = F.Frags[f];
			if (Fr.Body)
			{
				FadingFrag[f] = true;
				Spread = FMath::Max(Spread, FVector::Dist2D(Fr.Body->GetComponentLocation(), F.LandCenter));
				Awake += Fr.Body->IsAnyRigidBodyAwake() ? 1 : 0;
				Bodies.Remove(Fr.Body.Get());
				const bool bMoving = Fr.State != EFragState::Frozen && Fr.Body->IsAnyRigidBodyAwake();
				if (bMoving)
				{
					// Still moving (a stray, or past its deadline): it keeps its physics and fades as it goes, released
					// at the fade's end. Never teleported (review CP33: a set-down sank bodies resting on a pile).
					Fading.Add({Fr.Body, Fr.Comp, static_cast<float>(GetWorld()->GetTimeSeconds()), true}); // no mesh: released next tick
					Fr.Comp = nullptr;
					Fr.Body = nullptr;
					++StillReserved;
					continue;
				}
				Fr.Body->SetSimulatePhysics(false); // frozen where it lies, then shrinks away over PhysicsFadeSeconds
				Fr.Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (Fr.Comp)
				{
					Fading.Add({Fr.Body, Fr.Comp, static_cast<float>(GetWorld()->GetTimeSeconds())});
					Fr.Comp = nullptr;
				}
				Fr.Body = nullptr;
			}
			if (Fr.Comp && F.Frags.IsValidIndex(Fr.Parent) && FadingFrag[Fr.Parent])
			{
				// A leaf of an unshattered chunk rides on it and fades with it (review CP26: it vanished at landing).
				FadingFrag[f] = true;
				Fading.Add({nullptr, Fr.Comp, static_cast<float>(GetWorld()->GetTimeSeconds())});
				Fr.Comp = nullptr;
			}
			if (Fr.Comp)
			{
				ReleasePiece(Fr.Comp);
				Fr.Comp = nullptr;
			}
		}
		BodiesReserved = FMath::Max(0, BodiesReserved - (F.Frags.Num() - StillReserved)); // moving bodies hold theirs until released
		UE_LOG(LogTemp, Display, TEXT("DCPHYS rest %d chunks and leaves after %.1f s (%s), %d still awake, %d past deadline, %d strays, farthest %.0f cm from the shared rubble site"), F.Frags.Num(), F.BodyAge, F.StillFor > 0.f ? TEXT("still") : TEXT("timed out"), Awake, F.Late, F.Stray, Spread);
		F.Frags.Reset();
	}
	const float RevealNow = GetWorld()->GetTimeSeconds() - FallOffset;
	for (const FIntVector& C : F.RevealOnRest)
	{
		DeferredTerrain.Add({RevealNow, C});
	}
	F.RevealOnRest.Reset();
	const FVector Where = F.LandCenter + FVector(0, 0, 100.f);
	if (Fx)
	{
		const float Radius = UDCVoxelSettings::Get().Tuning.TerrainVoxelCm * 12.f;
		Fx->Emit(TEXT("Landing"), Where, Radius);
		Fx->Burn(TEXT("Rubble"), Where, Radius, UDCVoxelSettings::Get().Tuning.DecaySeconds);
	}
	StartFireLight(Where);
	if (F.Comp)
	{
		ReleasePiece(F.Comp);
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
	if (PendingCraft.IsValid() && PendingCraft->BuildId == GS->CityBuildId)
	{
		return; // this build is crafting off-thread already
	}
	if (GS->CityBuildId != BuiltId || (GS->CitySector == INDEX_NONE) != (BuiltSector == INDEX_NONE))
	{
		if (GS->CitySector == INDEX_NONE)
		{
			PendingCraft.Reset();
			Clear();
			BuiltId = GS->CityBuildId;
		}
		else
		{
			Build(GS->RunSeed, GS->CitySector, GS->CityBuildId, UDCVoxelSettings::Get().Tuning.bAsyncSectorCraft);
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
	if (PendingCraft.IsValid() && PendingCraft->Task.IsCompleted())
	{
		FlushPendingCraft(); // done off-thread: swap it in and go live (meshing then runs under the sector card)
	}
	if (BuiltSector != INDEX_NONE && Falling.Num() == 0)
	{
		PrewarmPieces(6); // a few pooled piece meshes per quiet frame until the body budget is covered
	}
	if (bFollowGameState) // the host drives Build and ApplyOp directly first, so following is a no-op there
	{
		FollowGameState();
	}
	// Two-phase ops: a planned op is held until its apply time (a strike's detonation), then committed.
	const ADCGameState* State = GetWorld()->GetGameState<ADCGameState>();
	double PrepMs = 0.0;
	double CommitMs = 0.0; // advisor CP26: the hitch log scopes the op's game-thread prepare and commit too
	if (InFlight.IsValid() && InFlightTask.IsCompleted())
	{
		const float Lead = State ? InFlight->Op.Time - State->Now() : 0.f;
		if (Lead > 0.f && !InFlight->bPrepared)
		{
			// Planned ahead: mesh it now (the worker is done writing), show it at the apply time.
			InFlight->bPrepared = true;
			bHoldUploads = true;
			const double P0 = FPlatformTime::Seconds();
			PrepareOp(InFlight->Op, InFlight->Result, InFlight->bAnimate, GetWorld()->GetTimeSeconds() + Lead);
			PrepMs = (FPlatformTime::Seconds() - P0) * 1000.0;
		}
		if (Lead <= 0.f)
		{
			const double C0 = FPlatformTime::Seconds();
			FinishInFlight();
			StartNextOp();
			CommitMs = (FPlatformTime::Seconds() - C0) * 1000.0;
		}
	}
	const double T0 = FPlatformTime::Seconds();
	PollJobs();
	const double T1 = FPlatformTime::Seconds();
	const int32 DirtyBefore = DirtyCells.Num();
	UploadCells();
	const double T2 = FPlatformTime::Seconds();
	// Falling pieces follow the presentation clock (a dying player's Last Frame slows them on that machine only, R21);
	// the voxel state and the host's records keep real time.
	FallOffset += DeltaSeconds * (1.f - UDCPresentationClock::Dilation(this));
	TickFalling(GetWorld()->GetTimeSeconds() - FallOffset);
	const double T3 = FPlatformTime::Seconds();
	TickCityShot();
	TickSignLights();
	TickFireLights();
	TickLampLights();
	TickCollapseCam();
	// Hitch log (R13): which stage of the city's frame went long.
	if ((T3 - T0) * 1000.0 + PrepMs + CommitMs > UDCVoxelSettings::Get().Tuning.HitchLogMs)
	{
		UE_LOG(LogTemp, Display, TEXT("DCHITCH city prep=%.1f commit=%.1f poll=%.1f upload=%.1f falling=%.1f crumb=%.1f ms (%d live) bodies=%d cells=%d->%d jobs=%d"), PrepMs, CommitMs, (T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (T3 - T2) * 1000.0,
			Crumbs ? Crumbs->LastTickMs : 0.f, Crumbs ? Crumbs->LiveCount() : 0, BodiesReserved, DirtyBefore, DirtyCells.Num(), Jobs.Num());
	}
}

void ADCVoxelCity::StartFireLight(const FVector& Where)
{
	// Aftermath by firelight (advisor CP9/CP11): a pooled, shadowless, flickering warm light over each new rubble pile
	// for the decay window, so settled rubble reads in the dark. The oldest light is reused past FireLights.
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (Tn.FireLights <= 0)
	{
		return;
	}
	int32 Slot = INDEX_NONE;
	float Oldest = TNumericLimits<float>::Max();
	for (int32 i = 0; i < FireLightPool.Num(); ++i)
	{
		const float Started = FireLightStart.IsValidIndex(i) ? FireLightStart[i] : -1e9f;
		if (Started < Oldest)
		{
			Oldest = Started;
			Slot = i;
		}
	}
	if (FireLightPool.Num() < Tn.FireLights)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->RegisterComponent();
		FireLightPool.Add(L);
		FireLightStart.Add(0.f);
		Slot = FireLightPool.Num() - 1;
	}
	if (!FireLightPool.IsValidIndex(Slot))
	{
		return;
	}
	UPointLightComponent* L = FireLightPool[Slot];
	L->SetWorldLocation(Where + FVector(0, 0, Tn.FireLightHeightCm));
	L->SetLightColor(Tn.FireLightColor);
	L->SetAttenuationRadius(Tn.FireLightRadiusCm);
	L->SetVisibility(true);
	FireLightStart[Slot] = GetWorld()->GetTimeSeconds();
}

void ADCVoxelCity::TickLampLights()
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextLampPick)
	{
		return;
	}
	NextLampPick = Now + Tn.SignLightRepickSeconds;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector View = PC->PlayerCameraManager->GetCameraLocation();
	TArray<FLightSpot> Near = LampSpots;
	Near.Sort([&View](const FLightSpot& A, const FLightSpot& B) { return FVector::DistSquared(A.At, View) < FVector::DistSquared(B.At, View); });
	const TArray<FDCPropLightDef>& Rows = UDCVoxelSettings::Get().PropLights;
	while (LampPool.Num() < FMath::Min(Tn.LampLights, Near.Num()))
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->RegisterComponent();
		LampPool.Add(L);
	}
	for (int32 i = 0; i < LampPool.Num(); ++i)
	{
		UPointLightComponent* L = LampPool[i];
		if (!Near.IsValidIndex(i) || i >= Tn.LampLights)
		{
			L->SetVisibility(false);
			continue;
		}
		const FDCPropLightDef* Row = Rows.IsValidIndex(Near[i].Row) ? &Rows[Near[i].Row] : nullptr;
		L->SetWorldLocation(Near[i].At + FVector(0, 0, Row ? Row->HeightCm : Tn.LampHeightCm));
		L->SetLightColor(Row ? Row->Color : Tn.LampColor);
		L->SetIntensity(Row ? Row->Candela : Tn.LampCandela);
		L->SetAttenuationRadius(Row ? Row->RadiusCm : Tn.LampRadiusCm);
		L->SetVisibility(true);
	}
}

void ADCVoxelCity::TickFireLights()
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = 0; i < FireLightPool.Num(); ++i)
	{
		UPointLightComponent* L = FireLightPool[i];
		const float Age = Now - FireLightStart[i];
		const float Life = FMath::Max(Tn.DecaySeconds, 0.1f);
		if (!L || Age >= Life)
		{
			if (L && L->IsVisible())
			{
				L->SetVisibility(false);
			}
			continue;
		}
		// Flicker from two incommensurate sines (no RNG state), fading out over the decay window.
		const float Flicker = 0.75f + 0.15f * FMath::Sin(Now * 13.f + i * 1.7f) + 0.1f * FMath::Sin(Now * 31.f + i * 0.9f);
		const float Fade = 1.f - Age / Life;
		L->SetIntensity(Tn.FireLightCandela * Flicker * Fade);
	}
}

void ADCVoxelCity::KillCarvedSigns(const TArray<int32>& TouchedLots)
{
	for (int32 Lot : TouchedLots)
	{
		if (!Sector.Towers.IsValidIndex(Lot))
		{
			continue;
		}
		const FDCVoxelTower& T = Sector.Towers[Lot];
		for (int32 s = 0; s < T.Signs.Num(); ++s)
		{
			if (T.Grid.Data.Get(T.Signs[s].Probe) != T.Signs[s].Material)
			{
				DeadSigns.Add(FIntPoint(Lot, s));
			}
		}
	}
}

void ADCVoxelCity::TickSignLights()
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextSignPick || Tn.SignLights <= 0)
	{
		return;
	}
	NextSignPick = Now + Tn.SignLightRepickSeconds;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager || BuiltId == 0)
	{
		return;
	}
	const FVector View = PC->PlayerCameraManager->GetCameraLocation();
	struct FPick
	{
		float Dist2;
		FVector At;
		FVector Normal;
		FVector2D SizeCm;
		uint8 Mat;
	};
	TArray<FPick> Picks;
	const float Max2 = FMath::Square(Tn.SignLightMaxDistanceCm);
	for (int32 Lot = 0; Lot < Sector.Towers.Num(); ++Lot)
	{
		const FDCVoxelTower& T = Sector.Towers[Lot];
		for (int32 s = 0; s < T.Signs.Num(); ++s)
		{
			const FDCVoxelSign& Sign = T.Signs[s];
			if (DeadSigns.Contains(FIntPoint(Lot, s)) || IsTowerFallen(Lot))
			{
				continue;
			}
			const FVector N(Sign.Normal);
			const FVector At = T.Grid.ToWorld(Sign.Center) + FVector(T.Grid.VoxelCm * 0.5f) + N * (T.Grid.VoxelCm * 0.5f + Tn.SignLightOffsetCm);
			const float D2 = FVector::DistSquared(At, View);
			if (D2 <= Max2)
			{
				Picks.Add({D2, At, N, FVector2D(Sign.WidthVox, Sign.HeightVox) * T.Grid.VoxelCm, Sign.Material});
			}
		}
	}
	Picks.Sort([](const FPick& A, const FPick& B) { return A.Dist2 < B.Dist2; });
	while (SignLightPool.Num() < FMath::Min(Tn.SignLights, Picks.Num()))
	{
		URectLightComponent* L = NewObject<URectLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->RegisterComponent();
		SignLightPool.Add(L);
	}
	for (int32 i = 0; i < SignLightPool.Num(); ++i)
	{
		URectLightComponent* L = SignLightPool[i];
		if (!Picks.IsValidIndex(i) || i >= Tn.SignLights)
		{
			L->SetVisibility(false);
			continue;
		}
		const FPick& P = Picks[i];
		L->SetWorldLocationAndRotation(P.At, P.Normal.Rotation());
		L->SetLightColor(Colors.IsValidIndex(P.Mat) ? FLinearColor::FromSRGBColor(Colors[P.Mat]) : FLinearColor::White);
		L->SetIntensity(Tn.SignLightCandela);
		L->SetAttenuationRadius(Tn.SignLightRadiusCm);
		L->SetSourceWidth(P.SizeCm.X * Tn.SignSourceScale);
		L->SetSourceHeight(P.SizeCm.Y * Tn.SignSourceScale);
		L->SetVisibility(true);
	}
}

void ADCVoxelCity::TickCollapseCam()
{
	// -dccollapsecam=<dir> (evidence only): when a strike commits, the view cuts to a fixed wide camera above the
	// collapse and writes frames at fixed times after the detonation (before, mid-fall, settled), then hands back.
	// The last two frames come from street height after the rest op, looking up at the skyline over the rubble
	// (critic V19.1: anything left hanging in the sky shows against it).
	static const float Times[] = {0.f, 0.3f, 0.6f, 1.0f, 1.5f, 2.2f, 3.2f, 4.5f, 6.5f, 9.f, 12.f};
	constexpr int32 FirstStreetFrame = 9;
	if (CollapseCamStep < 0)
	{
		return;
	}
	static FString Dir;
	static const bool bWanted = FParse::Value(FCommandLine::Get(), TEXT("dccollapsecam="), Dir);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!bWanted || !PC)
	{
		return;
	}
	if (!ShotCamera)
	{
		ShotCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
		ShotCamera->GetCameraComponent()->SetFieldOfView(60.f);
	}
	const float T = GetWorld()->GetTimeSeconds() - CollapseCamStart;
	const bool bStreet = CollapseCamStep >= FirstStreetFrame;
	const FVector Eye = CollapseCamCenter + (bStreet ? FVector(-3200.f, -2400.f, 250.f) : FVector(-4200.f, -4200.f, 6500.f));
	ShotCamera->SetActorLocationAndRotation(Eye, (CollapseCamCenter + FVector(0, 0, bStreet ? 1600.f : 800.f) - Eye).Rotation());
	if (PC->GetViewTarget() != ShotCamera)
	{
		PC->SetViewTargetWithBlend(ShotCamera, 0.f);
	}
	if (CollapseCamStep < UE_ARRAY_COUNT(Times) && T >= Times[CollapseCamStep])
	{
		FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("collapse_op%d_%02d_t%.1f.png"), CollapseCamOp, CollapseCamStep, Times[CollapseCamStep]), false, false);
		++CollapseCamStep;
	}
	if (CollapseCamStep >= UE_ARRAY_COUNT(Times) && T >= Times[UE_ARRAY_COUNT(Times) - 1] + 0.3f)
	{
		PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
		CollapseCamStep = -1;
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
