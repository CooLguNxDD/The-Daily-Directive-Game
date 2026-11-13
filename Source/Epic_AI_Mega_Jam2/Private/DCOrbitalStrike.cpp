#include "DCOrbitalStrike.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCBuilding.h"
#include "DCVoxelCity.h"
#include "Voxel/DCVoxelGrid.h"
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
	Ring = MakeFx(this, TEXT("Ring"), RootComponent);
	Beacon = MakeFx(this, TEXT("Beacon"), RootComponent);
	Fireball = MakeFx(this, TEXT("Fireball"), RootComponent);
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
	BeamMat = DCAssets::MakeFlat(this, BeamColor, 4.f);
	Beam->SetMaterial(0, BeamMat);
	Beam->SetRelativeLocation(FVector(0, 0, BeamHeightCm * 0.5f));

	Ring->SetStaticMesh(DCAssets::Cylinder());
	Ring->SetMaterial(0, DCAssets::MakeFlat(this, BeamColor * 0.35f, 1.5f));
	Ring->SetRelativeLocation(FVector(0, 0, 3.f));
	Ring->SetRelativeScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, 0.02f));

	Beacon->SetStaticMesh(DCAssets::Cone());
	Beacon->SetMaterial(0, DCAssets::MakeFlat(this, BeamColor, 30.f));
	Beacon->SetRelativeLocation(FVector(0, 0, 60.f));
	Beacon->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.2f));

	Fireball->SetStaticMesh(DCAssets::Sphere());
	FireMat = DCAssets::MakeFlat(this, UDCTuning::Get().Look.Fire, 80.f);
	Fireball->SetMaterial(0, FireMat);
	Fireball->SetVisibility(false);

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
	// The laser converges: wide and faint to a thin, white-hot line.
	const float Width = FMath::Lerp(7.f, 0.35f, FMath::Pow(U, 0.7f));
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Now * FMath::Lerp(6.f, 40.f, U));
	Beam->SetRelativeScale3D(FVector(Width, Width, BeamHeightCm / 100.f));
	DCAssets::SetFlat(BeamMat, Def.BeamColor, FMath::Lerp(2.f, 60.f, U * U) * (0.7f + 0.3f * Pulse));
	Beacon->SetVisibility(Pulse > 0.3f || U > 0.8f);

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
	const FVector Here = GetActorLocation();
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
	Fireball->SetVisibility(true);
	Shock->SetVisibility(true);
	Ring->SetVisibility(false);
	Beacon->SetVisibility(false);
}

void ADCOrbitalStrike::TickBlast(float Now, const FDCStratagemDef& Def)
{
	const float T = Now - DetonateTime;
	const float Grow = FMath::Clamp(T / FireballGrowSeconds, 0.f, 1.f);
	const float Fade = FMath::Clamp(1.f - (T - FireballGrowSeconds) / FireballLifeSeconds, 0.f, 1.f);
	const float Scale = Def.FireballScale * FMath::Sqrt(Grow) * (0.6f + 0.4f * Fade);
	Fireball->SetRelativeScale3D(FVector(Scale));
	Fireball->SetRelativeLocation(FVector(0, 0, Scale * 35.f + T * 300.f));
	DCAssets::SetFlat(FireMat, FMath::Lerp(FLinearColor(0.25f, 0.05f, 0.02f), FLinearColor(1.f, 0.85f, 0.6f), Fade * Fade), 2.f + UDCTuning::Get().Look.FireballGlow * Fade * Fade);
	Fireball->SetVisibility(Fade > 0.02f);

	const float ShockR = Def.BlastRadius * 2.4f * FMath::Clamp(T / 0.6f, 0.f, 1.f) / 100.f;
	Shock->SetRelativeScale3D(FVector(ShockR, ShockR, 0.04f));
	Shock->SetRelativeLocation(FVector(0, 0, 40.f));
	DCAssets::SetFlat(ShockMat, FLinearColor(1.f, 0.85f, 0.6f), UDCTuning::Get().Look.ShockGlow * FMath::Clamp(1.f - T / 0.6f, 0.f, 1.f));
	Shock->SetVisibility(T < 0.6f);

	const float BeamFade = FMath::Clamp(1.f - T / 0.35f, 0.f, 1.f);
	Beam->SetRelativeScale3D(FVector(3.f * BeamFade + 0.01f, 3.f * BeamFade + 0.01f, BeamHeightCm / 100.f));
	DCAssets::SetFlat(BeamMat, FLinearColor::White, 200.f * BeamFade);
	Beam->SetVisibility(BeamFade > 0.f);
	Light->SetIntensity(UDCTuning::Get().Look.BlastLightIntensity * Fade * Fade);
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

void ADCOrbitalStrike::DetonateHost(const FDCStratagemDef& Def)
{
	UWorld* World = GetWorld();
	ADCGameState* GS = World->GetGameState<ADCGameState>();
	const FVector Here = GetActorLocation();
	GS->AddBlast(Here, Def.BlastRadius, Def.Id);

	if (ADCVoxelCity* City = ADCVoxelCity::IsEnabled() ? GS->GetVoxelCity() : nullptr)
	{
		// Voxel city (R3): the strike is one op every machine applies; the structure pass decides which towers fall,
		// and those become the usual collapse records (scoring, domino, reel).
		FDCVoxelOp Op;
		Op.Kind = TEXT("Strike");
		Op.Stratagem = Def.Id;
		Op.CenterCm = DCVoxelMath::ToCm(FVector(Here.X, Here.Y, 0.f));
		Op.BlastRadiusCm = FMath::RoundToInt32(Def.BlastRadius);
		const int32 Index = GS->VoxelOps.Num();
		GS->AddVoxelOp(Op);
		const FDCVoxelOpResult Result = City->ApplyOp(GS->VoxelOps[Index], true);
		GS->VoxelOps[Index].ResultHash = City->GetHash();
		for (ADCBuilding* Building : TActorRange<ADCBuilding>(World))
		{
			ADCGameMode* GM = World->GetAuthGameMode<ADCGameMode>();
			if (GM && Result.FallenLots.Contains(Building->GetLotIndex()))
			{
				GM->ScheduleCollapse(Building, Here, TEXT("Strike"), 0.f);
			}
		}
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
