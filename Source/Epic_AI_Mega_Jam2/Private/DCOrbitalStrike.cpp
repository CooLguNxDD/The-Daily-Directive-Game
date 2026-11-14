#include "DCOrbitalStrike.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCBuilding.h"
#include "DCVoxelCity.h"
#include "Voxel/DCVoxelGrid.h"
#include "Voxel/DCVoxelSettings.h"
#include "DCCharacter.h"
#include "DCEnemy.h"
#include "DCEnvironment.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float BeamHeightCm = 40000.f;
	constexpr float FireballGrowSeconds = 0.12f;
	constexpr float FireballLifeSeconds = 2.2f;
	constexpr float LateFxCutoffSeconds = 1.0f;

	UStaticMeshComponent* MakeFx(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* M = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		M->SetupAttachment(Parent);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetMobility(EComponentMobility::Movable);
		return M;
	}
}

ADCOrbitalStrike::ADCOrbitalStrike()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Beam = MakeFx(this, TEXT("Beam"), RootComponent);
	Ring = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(RootComponent);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetMobility(EComponentMobility::Movable);
	Beacon = MakeFx(this, TEXT("Beacon"), RootComponent);
	ImpactFlare = MakeFx(this, TEXT("ImpactFlare"), RootComponent);
	Fireball = MakeFx(this, TEXT("Fireball"), RootComponent);
	FireCubes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FireCubes"));
	FireCubes->SetupAttachment(RootComponent);
	FireCubes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FireCubes->SetCastShadow(false);
	FireCubes->SetMobility(EComponentMobility::Movable);
	Shock = MakeFx(this, TEXT("Shock"), RootComponent);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(0, 0, 600));
	Light->SetIntensity(0.f);
	Light->SetAttenuationRadius(9000.f);
	Light->SetCastShadows(false);
}

void ADCOrbitalStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCOrbitalStrike, StratagemId);
	DOREPLIFETIME(ADCOrbitalStrike, DetonateTime);
}

void ADCOrbitalStrike::Init(FName InStratagemId, float InDetonateTime, AController* InInstigator)
{
	StratagemId = InStratagemId;
	DetonateTime = InDetonateTime;
	InstigatorController = InInstigator;
}

const FDCStratagemDef* ADCOrbitalStrike::GetDef() const
{
	return UDCTuning::Get().FindStratagem(StratagemId);
}

void ADCOrbitalStrike::BeginPlay()
{
	Super::BeginPlay();
	const FDCStratagemDef* Def = GetDef();
	if (bRestage)
	{
		SetReplicates(false);
	}
	if (Def && HasAuthority() && !bRestage && Def->Behaviour == TEXT("Decoy"))
	{
		// Decoy Paparazzi: the swarm mobs the beacon until it goes off.
		if (ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>())
		{
			GS->Lures.Add(TPair<FVector, float>(GetActorLocation(), DetonateTime + 0.3f));
		}
	}
	const FLinearColor BeamColor = Def ? Def->BeamColor : FLinearColor::Red;
	const float Radius = Def ? Def->BlastRadius : 1000.f;

	Beam->SetStaticMesh(DCAssets::Cylinder());
	BeamMat = DCAssets::MakeFlatUnlit(this, BeamColor, 1.f);
	Beam->SetMaterial(0, BeamMat);
	Beam->SetRelativeLocation(FVector(0, 0, BeamHeightCm * 0.5f));

	// The danger zone: a thin unlit band of propaganda red on the ground (a filled lit disc washed to salmon).
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	DCAssets::BuildRing(Ring, Radius);
	Ring->SetMaterial(0, DCAssets::MakeFlatUnlit(this, Look.Accent, Look.DangerRingGlow));
	Ring->SetRelativeLocation(FVector(0, 0, 3.f));

	ImpactFlare->SetStaticMesh(DCAssets::Cylinder());
	FlareMat = DCAssets::MakeFlatUnlit(this, UDCTuning::Get().Look.ImpactFlareColor, 1.f);
	ImpactFlare->SetMaterial(0, FlareMat);
	ImpactFlare->SetRelativeLocation(FVector(0, 0, 6.f));
	Beacon->SetStaticMesh(DCAssets::Cone());
	Beacon->SetMaterial(0, DCAssets::MakeFlat(this, BeamColor, 30.f));
	Beacon->SetRelativeLocation(FVector(0, 0, 60.f));
	Beacon->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.2f));

	Fireball->SetStaticMesh(DCAssets::Sphere());
	FireMat = UDCTuning::Get().Look.bFireCubesUnlit ? DCAssets::MakeFlatUnlit(this, UDCTuning::Get().Look.Fire, 1.f) : DCAssets::MakeFlat(this, UDCTuning::Get().Look.Fire, 80.f);
	CoreMat = DCAssets::MakeFlatUnlit(this, UDCTuning::Get().Look.BlastHotColor, UDCTuning::Get().Look.CoreFlareGlow);
	Fireball->SetMaterial(0, CoreMat); // the hot core flare (TickBlast)
	Fireball->SetCastShadow(false);
	Fireball->SetVisibility(false);
	// The core's cubes, seeded from where the strike landed: every machine sees the same burst.
	FireCubes->SetStaticMesh(DCAssets::Cube());
	FireCubes->SetMaterial(0, FireMat);
	FRandomStream CubeRng(GetTypeHash(FIntVector(GetActorLocation() / 10.f)));
	const int32 NumCubes = FMath::Max(UDCTuning::Get().Look.FireballCubes, 1);
	CubeSeeds.Reset();
	TArray<FTransform> Hidden;
	for (int32 i = 0; i < NumCubes; ++i)
	{
		const FVector Dir = CubeRng.VRand() * FVector(1.f, 1.f, 0.7f);
		const FVector2D CubeSize = UDCTuning::Get().Look.FireballCubeSize;
		CubeSeeds.Add(FVector4f(FVector3f(Dir * CubeRng.FRandRange(0.25f, 1.f)), CubeRng.FRandRange(CubeSize.X, CubeSize.Y)));
		Hidden.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
	}
	FireCubes->ClearInstances();
	FireCubes->AddInstances(Hidden, false);
	FireCubes->SetVisibility(false);

	Shock->SetStaticMesh(DCAssets::Cylinder());
	ShockMat = DCAssets::MakeFlat(this, FLinearColor(1.f, 0.9f, 0.7f), 40.f);
	Shock->SetMaterial(0, ShockMat);
	Shock->SetVisibility(false);

	Light->SetLightColor(UDCTuning::Get().Look.Fire);
	DCAudio::PlayAt(this, TEXT("Beacon"), GetActorLocation());
}

void ADCOrbitalStrike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const FDCStratagemDef* Def = GetDef();
	if (!GS || !Def)
	{
		return;
	}
	const float Now = GS->Now();
	if (Now < DetonateTime)
	{
		TickTelegraph(Now, *Def);
		if (HasAuthority() && !bRestage && !bOpQueued && Now >= DetonateTime - UDCVoxelSettings::Get().Tuning.PlanLeadSeconds)
		{
			QueueVoxelOp(*Def);
		}
		if (HasAuthority() && !bRestage)
		{
			const float U = 1.f - (DetonateTime - Now) / FMath::Max(Def->TelegraphSeconds, 0.01f);
			TickBehaviour(*Def, FMath::Clamp(U, 0.f, 1.f), DeltaSeconds);
		}
		return;
	}
	if (!bLocalDetonated)
	{
		bLocalDetonated = true;
		if (HasAuthority() && !bRestage)
		{
			DetonateHost(*Def);
		}
		if (Now - DetonateTime < LateFxCutoffSeconds)
		{
			DetonateLocal(*Def);
		}
	}
	TickBlast(Now, *Def);
}

void ADCOrbitalStrike::TickTelegraph(float Now, const FDCStratagemDef& Def)
{
	const float Start = DetonateTime - Def.TelegraphSeconds;
	const float U = FMath::Clamp((Now - Start) / FMath::Max(Def.TelegraphSeconds, 0.01f), 0.f, 1.f);
	// The beam narrows from a broad sighting column to a thin lance as it charges (art bible: thin red beams).
	const FDCLookTuning& BeamLook = UDCTuning::Get().Look;
	const float Width = FMath::Lerp(BeamLook.BeamWidthStartCm, BeamLook.BeamWidthEndCm, FMath::Pow(U, 0.7f)) / 100.f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Now * FMath::Lerp(6.f, 40.f, U));
	Beam->SetRelativeScale3D(FVector(Width, Width, BeamHeightCm / 100.f));
	DCAssets::SetFlat(BeamMat, Def.BeamColor, FMath::Lerp(1.f, UDCTuning::Get().Look.BeamGlowPeak, U * U) * (0.7f + 0.3f * Pulse)); // red, not blown to pink
	Beacon->SetVisibility(Pulse > 0.3f || U > 0.8f);
	// The flare where the beam lands: wider than the beam, hotter as it charges, pulsing with it.
	const FDCLookTuning& FlareLook = UDCTuning::Get().Look;
	const float FlareD = Width * FlareLook.ImpactFlareWidthScale * (0.85f + 0.3f * Pulse);
	ImpactFlare->SetRelativeScale3D(FVector(FlareD, FlareD, 0.02f));
	DCAssets::SetFlat(FlareMat, FlareLook.ImpactFlareColor, FMath::Lerp(0.6f, FlareLook.ImpactFlareGlow, U) * (0.6f + 0.4f * Pulse));
	// The beam lights the street and facades around its target (red contact light, rising with the charge).
	Light->SetRelativeLocation(FVector(0, 0, 150.f));
	Light->SetLightColor(Def.BeamColor);
	Light->SetAttenuationRadius(FMath::Max(Def.BlastRadius * FlareLook.BeamContactReach, 600.f));
	Light->SetIntensity(FlareLook.BeamContactLight * U * U * (0.7f + 0.3f * Pulse));

	if (!bChargePlayed && U > 0.f && Def.TelegraphSeconds - (Now - Start) <= 3.2f)
	{
		bChargePlayed = true;
		DCAudio::PlayAt(this, TEXT("LaserCharge"), GetActorLocation(), 1.f, 3.2f / FMath::Max(DetonateTime - Now, 0.5f));
	}
	if (Now >= NextBeepTime)
	{
		DCAudio::PlayAt(this, TEXT("Telegraph"), GetActorLocation(), 0.8f, 1.f + U * 0.5f);
		NextBeepTime = Now + FMath::Lerp(0.5f, 0.07f, FMath::Pow(U, 1.5f));
	}
}

void ADCOrbitalStrike::DetonateLocal(const FDCStratagemDef& Def)
{
	// The blast light takes over from the beam's contact light: fire colour. It rims the target from behind (seen from
	// this machine's camera) rather than flooding the facade the camera looks at.
	const FDCLookTuning& BlastLook = UDCTuning::Get().Look;
	const FVector Here = GetActorLocation();
	FVector Away = FVector::ZeroVector;
	if (const APlayerController* ViewPC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector Eye;
		FRotator EyeRot;
		ViewPC->GetPlayerViewPoint(Eye, EyeRot);
		Away = FVector(Here.X - Eye.X, Here.Y - Eye.Y, 0.f).GetSafeNormal();
	}
	Light->SetRelativeLocation(Away * Def.BlastRadius * BlastLook.BlastLightBehind + FVector(0, 0, BlastLook.BlastLightHeightCm));
	Light->SetLightColor(BlastLook.Fire);
	Light->SetAttenuationRadius(BlastLook.BlastLightRadiusScale > 0.f ? Def.BlastRadius * BlastLook.BlastLightRadiusScale : BlastLook.BlastLightRadiusCm);
	float Proximity = 0.4f;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		Proximity = FMath::Clamp(1.f - FVector::Dist(ViewLoc, Here) / 25000.f, 0.35f, 1.f);
		if (ADCCharacter* Me = Cast<ADCCharacter>(PC->GetPawn()))
		{
			Me->AddTrauma(Proximity * 1.2f);
		}
	}
	if (const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>())
	{
		if (ADCEnvironment* Env = GS->GetEnvironment())
		{
			Env->Flash(Def.BloomPeak * Proximity, 3.f * Proximity);
		}
	}
	DCAudio::PlayAt(this, TEXT("Detonation"), Here, 1.f, FMath::FRandRange(0.9f, 1.05f));
	DCAudio::Play2D(this, TEXT("Detonation"), 0.35f * Proximity, 0.8f);
	FireCubes->SetVisibility(true);
	Shock->SetVisibility(false); // the filled shock disc read as a white bar edge-on (critic); the voxel core carries the peak
	Ring->SetVisibility(false);
	Beacon->SetVisibility(false);
	ImpactFlare->SetVisibility(false);
}

void ADCOrbitalStrike::TickBlast(float Now, const FDCStratagemDef& Def)
{
	const float T = Now - DetonateTime;
	const float Grow = FMath::Clamp(T / FireballGrowSeconds, 0.f, 1.f);
	const float Fade = FMath::Clamp(1.f - (T - FireballGrowSeconds) / FireballLifeSeconds, 0.f, 1.f);
	const float Scale = Def.FireballScale * FMath::Sqrt(Grow) * (0.6f + 0.4f * Fade);
	// Voxel burst: cubes fly out from the core, rise with the heat and shrink as the fire cools; the gaps between
	// them keep the target readable at the peak (the smooth sphere hid it).
	const FVector Core(0, 0, Scale * 35.f + T * 300.f);
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	// Cubes against the lens shrink away (a big hot cube near the camera washes the frame at the peak).
	FVector Eye = FVector(1e9f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Eye = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	const FTransform& ToWorld = FireCubes->GetComponentTransform();
	const float NearSpan = FMath::Max(Look.FireCubeNearFadeCm - Look.FireCubeNearHideCm, 1.f);
	TArray<FTransform> Xf;
	Xf.Reserve(CubeSeeds.Num());
	for (int32 i = 0; i < CubeSeeds.Num(); ++i)
	{
		const FVector4f& C = CubeSeeds[i];
		const float Out = 0.35f + 0.65f * FMath::Sqrt(Grow) + 0.25f * (1.f - Fade);
		const FVector Pos = Core + FVector(C.X, C.Y, C.Z) * Scale * 50.f * Out + FVector(0, 0, T * 120.f * (1.f + C.W));
		const float Near = FMath::Clamp((FVector::Dist(ToWorld.TransformPosition(Pos), Eye) - Look.FireCubeNearHideCm) / NearSpan, 0.f, 1.f);
		const float Size = Scale * C.W * FMath::Max(Fade, 0.f) * (0.6f + 0.4f * Grow) * Near;
		Xf.Add(FTransform(FRotator(i * 37.f + T * 90.f, i * 53.f, i * 11.f), Pos, FVector(Size)));
	}
	FireCubes->BatchUpdateInstancesTransforms(0, Xf, false, true, true);
	// The hot core: white-gold, far over the bloom threshold (a halo), shrinking away within CoreFlareSeconds.
	{
		const float CoreU = FMath::Clamp(T / FMath::Max(Look.CoreFlareSeconds, 0.01f), 0.f, 1.f);
		const float CoreD = Look.CoreFlareMeters * (1.f - CoreU * CoreU) * FMath::Clamp(T / 0.04f, 0.2f, 1.f);
		Fireball->SetRelativeLocation(FVector(0, 0, Look.CoreFlareHeightCm));
		Fireball->SetRelativeScale3D(FVector(CoreD));
		DCAssets::SetFlat(CoreMat, Look.BlastHotColor, Look.CoreFlareGlow * (1.f - CoreU));
		Fireball->SetVisibility(CoreU < 1.f && CoreD > 0.05f);
	}
	// Blast white-orange only at the first instant, then orange to ember red: the peak reads as fire, not a white dome.
	const FLinearColor Hot = FMath::Lerp(Look.BlastFireColor, Look.BlastHotColor, FMath::Clamp(1.f - T / 0.25f, 0.f, 1.f));
	DCAssets::SetFlat(FireMat, FMath::Lerp(Look.BlastEmberColor, Hot, Fade * Fade), 1.f + Look.FireballGlow * Fade * Fade * Fade);
	FireCubes->SetVisibility(Fade > 0.02f);

	const float ShockR = Def.BlastRadius * 2.4f * FMath::Clamp(T / 0.6f, 0.f, 1.f) / 100.f;
	Shock->SetRelativeScale3D(FVector(ShockR, ShockR, 0.04f));
	Shock->SetRelativeLocation(FVector(0, 0, 40.f));
	DCAssets::SetFlat(ShockMat, FLinearColor(1.f, 0.85f, 0.6f), UDCTuning::Get().Look.ShockGlow * FMath::Clamp(1.f - T / 0.6f, 0.f, 1.f));
	Shock->SetVisibility(false);

	const float BeamFade = FMath::Clamp(1.f - T / 0.35f, 0.f, 1.f);
	const float FlashWidth = Look.BeamFlashWidthCm / 100.f * BeamFade + 0.01f; // a lance flash, not a pillar filling the lens
	Beam->SetRelativeScale3D(FVector(FlashWidth, FlashWidth, BeamHeightCm / 100.f));
	DCAssets::SetFlat(BeamMat, FMath::Lerp(Def.BeamColor, Look.BeamFlashColor, 0.5f), Look.BeamFlashGlow * BeamFade); // a short orange flash, then gone
	Beam->SetVisibility(BeamFade > 0.f);
	// The blast light: a hard fall from the peak, then a small warm tail that lights the rubble through the decay.
	const float LightFade = FMath::Clamp(1.f - (T - FireballGrowSeconds) / FMath::Max(Look.BlastLightSeconds, 0.1f), 0.f, 1.f);
	const float Tail = FMath::Clamp(Look.BlastLightTailFraction, 0.f, 1.f);
	Light->SetIntensity(Look.BlastLightIntensity * (LightFade * LightFade * LightFade * (1.f - Tail) + Tail * FMath::Sqrt(LightFade)));
}

void ADCOrbitalStrike::TickBehaviour(const FDCStratagemDef& Def, float U, float DeltaSeconds)
{
	using FBehaviourFn = void (*)(ADCOrbitalStrike&, const FDCStratagemDef&, float, float);
	static const TMap<FName, FBehaviourFn> Behaviours = {
		// Gravity Singularity: everything nearby is dragged into the blast, harder as it charges.
		{TEXT("Singularity"), [](ADCOrbitalStrike& S, const FDCStratagemDef& D, float Charge, float Dt)
			{
				const FVector Here = S.GetActorLocation();
				for (ADCEnemy* E : TActorRange<ADCEnemy>(S.GetWorld()))
				{
					const FVector To = Here - E->GetActorLocation();
					if (E->IsAlive() && !E->IsWalker() && To.Size2D() < D.BlastRadius * 2.5f)
					{
						E->SetActorLocation(E->GetActorLocation() + To.GetClampedToMaxSize(Dt * (600.f + 2400.f * Charge)));
					}
				}
			}},
	};
	if (const FBehaviourFn* Fn = Behaviours.Find(Def.Behaviour))
	{
		(*Fn)(*this, Def, U, DeltaSeconds);
	}
}

void ADCOrbitalStrike::QueueVoxelOp(const FDCStratagemDef& Def)
{
	bOpQueued = true;
	ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCVoxelCity* City = GS && ADCVoxelCity::IsEnabled() ? GS->GetVoxelCity() : nullptr;
	if (!City)
	{
		return;
	}
	// Voxel city (R3): the strike is one op every machine applies; the structure pass decides which towers fall, and
	// those become the usual collapse records (scoring, domino, reel). Two-phase (C3.1): appended PlanLeadSeconds
	// before detonation, planned on the worker, committed by every city at the detonation time, so the crater, the
	// falls and the host's collapse records land in the detonation frame.
	const FVector Here = GetActorLocation();
	FDCVoxelOp Op;
	Op.Kind = TEXT("Strike");
	Op.Stratagem = Def.Id;
	Op.CenterCm = DCVoxelMath::ToCm(FVector(Here.X, Here.Y, 0.f));
	Op.BlastRadiusCm = FMath::RoundToInt32(Def.BlastRadius);
	const int32 Seq = GS->AddVoxelOp(Op, FMath::Max(DetonateTime, GS->Now())).Seq;
	TWeakObjectPtr<ADCGameState> WeakGS(GS);
	const FVector Epicenter = Here;
	const float OpTime = GS->VoxelOps.Last().Time;
	City->ApplyOpAsync(GS->VoxelOps.Last(), true, [WeakGS, Seq, Epicenter, OpTime](const FDCVoxelOpResult& Result, uint32 Hash)
	{
		ADCGameState* State = WeakGS.Get();
		if (!State)
		{
			return;
		}
		if (FDCVoxelOp* Rec = State->VoxelOps.FindByPredicate([Seq](const FDCVoxelOp& O) { return O.Seq == Seq; }))
		{
			Rec->ResultHash = Hash;
		}
		ADCGameMode* GM = State->GetWorld()->GetAuthGameMode<ADCGameMode>();
		for (ADCBuilding* Building : TActorRange<ADCBuilding>(State->GetWorld()))
		{
			if (GM && Result.FallenLots.Contains(Building->GetLotIndex()))
			{
				GM->ScheduleCollapse(Building, Epicenter, TEXT("Strike"), 0.f, OpTime); // at the blast, not when the worker finished
			}
		}
	});
}

void ADCOrbitalStrike::DetonateHost(const FDCStratagemDef& Def)
{
	UWorld* World = GetWorld();
	ADCGameState* GS = World->GetGameState<ADCGameState>();
	const FVector Here = GetActorLocation();
	GS->AddBlast(Here, Def.BlastRadius, Def.Id);
	if (!bOpQueued)
	{
		QueueVoxelOp(Def); // a telegraph shorter than the plan lead: plan now, commit as soon as it is ready
	}

	for (ADCBuilding* Building : TActorRange<ADCBuilding>(World))
	{
		if (ADCVoxelCity::IsEnabled())
		{
			break; // block city only: every tower near the blast collapses whole
		}
		const FDCBuildingSpec& Spec = Building->GetSpec();
		const float HalfFootprint = FMath::Max(Spec.SizeX, Spec.SizeY) * Spec.BlockSize * 0.5f;
		if (FVector::Dist2D(Building->GetActorLocation(), Here) <= Def.BlastRadius + HalfFootprint)
		{
			if (ADCGameMode* GM = World->GetAuthGameMode<ADCGameMode>())
			{
				GM->ScheduleCollapse(Building, Here, TEXT("Strike"), 0.f); // the strike's own record is the event
			}
		}
	}

	// Evidence: how many enemies the blast caught (Decoy lures and Singularity pulls show up as larger counts).
	int32 Caught = 0;
	for (ADCEnemy* E : TActorRange<ADCEnemy>(World))
	{
		Caught += E->IsAlive() && FVector::Dist(E->GetActorLocation(), Here) < Def.BlastRadius ? 1 : 0;
	}
	UE_LOG(LogTemp, Display, TEXT("DCBLAST %s radius=%.0f enemies caught=%d"), *Def.Id.ToString(), Def.BlastRadius, Caught);

	TArray<AActor*> Ignore;
	UGameplayStatics::ApplyRadialDamage(this, Def.Damage, Here, Def.BlastRadius, UDamageType::StaticClass(), Ignore, this, InstigatorController, true, ECC_WorldStatic);

	for (ADCCharacter* Player : TActorRange<ADCCharacter>(World))
	{
		const float Dist = FVector::Dist(Player->GetActorLocation(), Here);
		if (Dist < Def.PlayerHurtRadius)
		{
			Player->HostApplyHurt(Def.PlayerDamage * (1.f - Dist / Def.PlayerHurtRadius), Here);
		}
	}

	if (ADCGameMode* GM = World->GetAuthGameMode<ADCGameMode>())
	{
		GM->RequestHitstop();
	}
	SetLifeSpan(FireballLifeSeconds + 1.5f);
}
