#include "DCCompanion.h"
#include "DCFrameScopes.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCCharacter.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "DCUnits.h"
#include "DCVoxelCity.h"
#include "DCVoxelFx.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Rules/DCCompanionRules.h"
#include "Rules/DCFactionRules.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	float ServerNow(const UWorld* World)
	{
		const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
		return GS ? GS->Now() : (World ? World->GetTimeSeconds() : 0.f);
	}

	/** Seconds the companion keeps facing its last shot before it turns back along its path. */
	constexpr float AimHoldSeconds = 0.8f;
}

ADCCompanion::ADCCompanion()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false); // every machine places its own copy from the owner and the anchor
	SetNetUpdateFrequency(10.f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Primitive = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Primitive"));
	Primitive->SetupAttachment(Root);
	Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Primitive->SetCastShadow(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetAttenuationRadius(500.f);
	Light->SetIntensity(0.f);
	Light->SetCastShadows(false);
	SetActorHiddenInGame(true);
}

void ADCCompanion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCCompanion, Kind);
	DOREPLIFETIME(ADCCompanion, bActive);
	DOREPLIFETIME(ADCCompanion, OwnerState);
	DOREPLIFETIME(ADCCompanion, Home);
	DOREPLIFETIME(ADCCompanion, Slot);
	DOREPLIFETIME(ADCCompanion, Slots);
	DOREPLIFETIME(ADCCompanion, ActivatedAt);
}

const FDCCompanionDef* ADCCompanion::GetDef() const
{
	return UDCTuning::Get().FindCompanion(Kind);
}

void ADCCompanion::HostActivate(FName InKind, ADCPlayerState* InOwner, const FVector& InHome, int32 InSlot, int32 InSlots, float Now)
{
	Kind = InKind;
	OwnerState = InOwner;
	Home = InHome;
	Slot = static_cast<uint8>(FMath::Clamp(InSlot, 0, 255));
	Slots = static_cast<uint8>(FMath::Clamp(InSlots, 1, 255));
	ActivatedAt = Now;
	NextShotTime = Now + 0.5f;
	bActive = true;
	ApplyState();
	ForceNetUpdate();
}

void ADCCompanion::HostRelease()
{
	bActive = false;
	OwnerState = nullptr;
	ApplyState();
	ForceNetUpdate();
}

void ADCCompanion::OnRep_State()
{
	ApplyState();
}

void ADCCompanion::ApplyState()
{
	const FDCCompanionDef* Def = GetDef();
	const bool bShow = bActive && Def;
	SetActorHiddenInGame(!bShow);
	SetActorTickEnabled(bShow);
	Light->SetIntensity(bShow ? 900.f : 0.f);
	if (!bShow || BuiltKind == Kind)
	{
		return;
	}
	BuiltKind = Kind;
	Light->SetLightColor(Def->Color);
	const FDCVoxelMeshData* Mesh = Def->Model.IsNone() ? nullptr : DCVoxelModels::Mesh(Def->Model);
	if (Mesh && !Model)
	{
		Model = NewObject<UProceduralMeshComponent>(this, TEXT("Model"));
		Model->SetupAttachment(Root);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->SetCastShadow(false);
		Model->RegisterComponent();
	}
	if (Mesh)
	{
		Model->ClearAllMeshSections();
		Model->CreateMeshSection(0, Mesh->Positions, Mesh->Indices, Mesh->Normals, Mesh->UVs, Mesh->Colors, {}, false);
		Model->SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
		Model->SetRelativeScale3D(FVector(Def->Scale));
	}
	if (Model)
	{
		Model->SetVisibility(Mesh != nullptr);
	}
	// No model (or a missing one): a glowing primitive in the row's colour stands in.
	Primitive->SetStaticMesh(Mesh ? nullptr : DCAssets::Sphere());
	Primitive->SetMaterial(0, DCAssets::MakeFlat(this, Def->Color, Def->Glow));
	Primitive->SetRelativeScale3D(FVector(0.5f * Def->Scale));
}

bool ADCCompanion::Anchor(const FDCCompanionDef& Def, FVector& Out) const
{
	if (!Def.bFollowsOwner)
	{
		Out = Home;
		return true;
	}
	const APawn* Pawn = OwnerState ? OwnerState->GetPawn() : nullptr;
	Out = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	return Pawn != nullptr;
}

void ADCCompanion::Tick(float DeltaSeconds)
{
	DC_FRAME_SCOPE("Companion");
	Super::Tick(DeltaSeconds);
	const FDCCompanionDef* Def = GetDef();
	FVector At;
	if (!bActive || !Def || !Anchor(*Def, At))
	{
		return;
	}
	const float Now = ServerNow(GetWorld());
	const FVector Pos = DCCompanionRules::Position(*Def, At, Now, Slot, Slots);
	const FVector Moved = Pos - GetActorLocation();
	SetActorLocation(Pos);
	const bool bAiming = Now - LastShotAt < AimHoldSeconds;
	const FVector Face = bAiming ? LastAim : (Moved.SizeSquared2D() > 1.f ? Moved : GetActorForwardVector());
	SetActorRotation(FRotator(0.f, Face.Rotation().Yaw, 0.f));
	if (!HasAuthority())
	{
		return;
	}
	if (Def->LifeSeconds > 0.f && Now >= ActivatedAt + Def->LifeSeconds)
	{
		HostRelease();
		return;
	}
	if (Now >= NextShotTime)
	{
		NextShotTime = Now + FMath::Max(Def->FireSeconds, 0.05f);
		HostShoot(*Def, Pos + FVector(0.f, 0.f, Def->MuzzleCm), Now);
	}
}

void ADCCompanion::HostShoot(const FDCCompanionDef& Def, const FVector& From, float Now)
{
	const UDCUnits* Units = UDCUnits::Get(this);
	if (!Units)
	{
		return;
	}
	const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
	TArray<ADCEnemy*> Foes;
	TArray<FVector> At;
	for (ADCEnemy* U : Units->All())
	{
		// Crew-hostile only: a faction that fights the crew, or a faction-less wave unit. The friendly army is spared.
		if (U && U->IsAlive() && (U->GetFaction().IsNone() || DCFactionRules::FightsCrew(Factions, U->GetFaction())))
		{
			Foes.Add(U);
			At.Add(U->GetActorLocation());
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCCompanionShot), false, this);
	Params.AddIgnoredActor(OwnerState ? OwnerState->GetPawn() : nullptr);
	// The nearest few in range, first clear line wins (a wall between keeps the companion quiet this beat).
	for (int32 Try = 0; Try < 3; ++Try)
	{
		const int32 Pick = DCCompanionRules::PickTarget(From, Def.RangeCm, At);
		if (Pick == INDEX_NONE)
		{
			return;
		}
		ADCEnemy* Foe = Foes[Pick];
		const FVector To = At[Pick];
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) && Hit.GetActor() != Foe;
		if (!bBlocked)
		{
			AController* Credit = OwnerState ? Cast<AController>(OwnerState->GetOwner()) : nullptr;
			Foe->TakeDamage(Def.Damage, FDamageEvent(), Credit, this);
			MulticastShot(From, To);
			return;
		}
		Foes.RemoveAt(Pick);
		At.RemoveAt(Pick);
	}
}

void ADCCompanion::MulticastShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize To)
{
	const FDCCompanionDef* Def = GetDef();
	if (!Def)
	{
		return;
	}
	LastAim = FVector(To) - FVector(From);
	LastShotAt = ServerNow(GetWorld());
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	if (ADCVoxelFx* Fx = City ? City->GetFx() : nullptr)
	{
		Fx->Tracer(From, To, Def->Color, UDCVoxelSettings::Get().Tuning.TracerGlow);
	}
	DCAudio::PlayAt(this, Def->ShotSound, From, Def->ShotVolume, Def->ShotPitch * FMath::FRandRange(0.95f, 1.05f));
}

// ---------------------------------------------------------------- pool

UDCCompanions* UDCCompanions::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDCCompanions>() : nullptr;
}

void UDCCompanions::Prewarm(int32 N)
{
	const int32 Target = FMath::Min(N, UDCTuning::Get().CompanionPoolMax);
	while (Pool.Num() < Target)
	{
		ADCCompanion* C = GetWorld()->SpawnActor<ADCCompanion>(ADCCompanion::StaticClass(), FTransform::Identity);
		if (!C)
		{
			return;
		}
		Pool.Add(C);
	}
}

ADCCompanion* UDCCompanions::Acquire()
{
	Pool.RemoveAll([](const TObjectPtr<ADCCompanion>& C) { return !IsValid(C); });
	for (ADCCompanion* C : Pool)
	{
		if (!C->IsActive())
		{
			return C;
		}
	}
	const int32 Before = Pool.Num();
	Prewarm(Before + 1);
	return Pool.Num() > Before ? Pool.Last().Get() : nullptr;
}

void UDCCompanions::ReleaseAll()
{
	for (ADCCompanion* C : Pool)
	{
		if (IsValid(C) && C->IsActive())
		{
			C->HostRelease();
		}
	}
}

TArray<ADCCompanion*> UDCCompanions::ActiveOf(const ADCPlayerState* Owner, FName Kind) const
{
	TArray<ADCCompanion*> Out;
	for (ADCCompanion* C : Pool)
	{
		if (IsValid(C) && C->IsActive() && C->GetOwnerState() == Owner && C->GetKind() == Kind)
		{
			Out.Add(C);
		}
	}
	Out.Sort([](const ADCCompanion& A, const ADCCompanion& B) { return A.GetActivatedAt() < B.GetActivatedAt(); });
	return Out;
}

int32 UDCCompanions::HostLaunchOrbiters(ADCPlayerState& Owner, const FDCCompanionDef& Def, int32 Count)
{
	const float Now = ServerNow(GetWorld());
	int32 Up = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		ADCCompanion* C = Acquire();
		if (!C)
		{
			break;
		}
		C->HostActivate(Def.Id, &Owner, FVector::ZeroVector, i, Count, Now);
		++Up;
	}
	UE_LOG(LogTemp, Display, TEXT("DCCOMPANION %s launched %d x %s"), *Owner.GetPlayerName(), Up, *Def.Id.ToString());
	return Up;
}

bool UDCCompanions::HostDeploy(ADCCharacter& Player, ADCPlayerState& Owner, const FDCCompanionDef& Def)
{
	UWorld* World = GetWorld();
	const FVector Centre = Player.GetActorLocation();
	// The same key revives: a downed teammate within reach takes priority over a deploy.
	for (const ADCCharacter* Other : TActorRange<ADCCharacter>(World))
	{
		if (Other != &Player && Other->IsDowned() && FVector::Dist(Other->GetActorLocation(), Centre) < Def.BlockNearDownedCm)
		{
			return false;
		}
	}
	const float FeetZ = Centre.Z - Player.GetSimpleCollisionHalfHeight();
	const FVector Aim = DCCompanionRules::PlaceAim(Def, FVector(Centre.X, Centre.Y, FeetZ), Player.GetControlRotation().Vector());
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCCompanionDeploy), false, &Player);
	const FVector Top = Aim + FVector(0, 0, Def.MaxStepCm + 50.f);
	const bool bGround = World->LineTraceSingleByChannel(Hit, Top, Aim - FVector(0, 0, Def.MaxStepCm + 50.f), ECC_Visibility, Params);
	// Clear line from the player's chest to the spot: never through a wall.
	FHitResult Wall;
	const bool bWall = World->LineTraceSingleByChannel(Wall, Centre, Top, ECC_Visibility, Params);
	if (!bGround || bWall || !DCCompanionRules::PlaceOk(Def, FeetZ, Hit.ImpactPoint.Z))
	{
		UE_LOG(LogTemp, Display, TEXT("DCCOMPANION %s deploy %s refused ground=%d wall=%d"), *Owner.GetPlayerName(), *Def.Id.ToString(), bGround ? 1 : 0, bWall ? 1 : 0);
		return false;
	}
	TArray<ADCCompanion*> Mine = ActiveOf(&Owner, Def.Id);
	for (int32 i = 0; i + FMath::Max(Def.MaxAlive, 1) <= Mine.Num(); ++i)
	{
		Mine[i]->HostRelease(); // the oldest goes back to the pool
	}
	ADCCompanion* C = Acquire();
	if (!C)
	{
		return false;
	}
	C->HostActivate(Def.Id, &Owner, Hit.ImpactPoint, 0, 1, ServerNow(World));
	UE_LOG(LogTemp, Display, TEXT("DCCOMPANION %s deployed %s at %s"), *Owner.GetPlayerName(), *Def.Id.ToString(), *Hit.ImpactPoint.ToCompactString());
	return true;
}
