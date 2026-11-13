#include "DCEnemy.h"

#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCDebrisPool.h"
#include "DCEnvironment.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName Swarmer(TEXT("Swarmer"));
	const FName Spotlight(TEXT("Spotlight"));
	const FName Walker(TEXT("Walker"));

	float GroundHeightBelow(UWorld* World, const FVector& P, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DCEnemyGround), false, Ignore);
		if (World->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 6000), P - FVector(0, 0, 6000), ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
		return 0.f;
	}

	void TickSwarmer(ADCEnemy& E, const FDCEnemyDef& Def, float Dt, float Now)
	{
		const float Aggro = UDCTuning::Get().Camera.AggroRadiusCm;
		FVector Lure;
		if (E.FindLure(Def.SenseRange * 2.f, Lure))
		{
			// Decoy Paparazzi: the swarm mobs the beacon instead of the players.
			E.bRushing = true;
			E.SteerTowards(Lure + FVector(0, 0, 60.f), Def.Speed * Def.RushMultiplier, Dt, Def.HoverHeight);
			return;
		}
		ADCCharacter* Target = E.FindTarget(FMath::Max(Def.SenseRange, Aggro), true);
		const ADCCharacter* Hunted = Target ? nullptr : E.FindTarget(Def.HuntRangeCm, false);
		if (Hunted)
		{
			// Out of sense range: the swarm drifts toward the nearest player, so nobody can camp with the rifle.
			E.SteerTowards(Hunted->GetActorLocation() + FVector(0, 0, 40.f), Def.Speed * Def.HuntSpeedScale, Dt, Def.HoverHeight);
			E.bRushing = false;
			return;
		}
		if (!Target)
		{
			E.Heading = E.Heading.RotateAngleAxis(25.f * Dt, FVector::UpVector);
			E.SteerTowards(E.GetActorLocation() + E.Heading * 600.f, Def.Speed * 0.4f, Dt, Def.HoverHeight);
			E.bRushing = false;
			return;
		}
		const bool bRush = Target->IsCameraRaised();
		if (bRush && !E.bRushing)
		{
			E.Cue(TEXT("SwarmerAggro"));
		}
		E.bRushing = bRush;
		E.SteerTowards(Target->GetActorLocation() + FVector(0, 0, 40.f), Def.Speed * (bRush ? Def.RushMultiplier : 1.f), Dt, Def.HoverHeight);
		E.TryAttack(Target, Now);
	}

	void TickSpotlight(ADCEnemy& E, const FDCEnemyDef& Def, float Dt, float Now)
	{
		ADCCharacter* Target = E.FindTarget(Def.SenseRange, false);
		if (!Target)
		{
			E.Heading = E.Heading.RotateAngleAxis(10.f * Dt, FVector::UpVector);
			E.SteerTowards(E.GetActorLocation() + E.Heading * 400.f, Def.Speed * 0.3f, Dt, Def.HoverHeight);
			E.LightTarget = E.GetActorLocation() + E.Heading * 1500.f - FVector(0, 0, Def.HoverHeight);
			return;
		}
		// Hold a stand-off orbit around the target and keep it in the beam.
		const FVector ToMe = (E.GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
		const FVector Orbit = ToMe.RotateAngleAxis(20.f, FVector::UpVector) * 1800.f;
		E.SteerTowards(Target->GetActorLocation() + Orbit, Def.Speed, Dt, Def.HoverHeight);
		E.LightTarget = FMath::VInterpTo(E.LightTarget, Target->GetActorLocation(), Dt, 2.5f);
		const float Dist = FVector::Dist(E.GetActorLocation(), Target->GetActorLocation());
		const FVector Beam = (FVector(E.LightTarget) - E.GetActorLocation()).GetSafeNormal();
		const FVector ToTarget = (Target->GetActorLocation() - E.GetActorLocation()).GetSafeNormal();
		const bool bInCone = FVector::DotProduct(Beam, ToTarget) > FMath::Cos(FMath::DegreesToRadians(Def.LightHalfAngle));
		if (Dist < Def.LightRange && bInCone)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DCSpotLos), false, &E);
			Params.AddIgnoredActor(Target);
			if (!E.GetWorld()->LineTraceSingleByChannel(Hit, E.GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params))
			{
				Target->HostSetSpotlit(Now + 0.35f);
			}
		}
		if (Now >= E.NextCueTime)
		{
			E.NextCueTime = Now + 2.5f;
			E.Cue(TEXT("Spotlight"));
		}
	}

	void TickWalker(ADCEnemy& E, const FDCEnemyDef& Def, float Dt, float Now)
	{
		// R11: a walker marches on the nearest standing tower and shoulders it over; with none left it hunts players.
		ADCCharacter* Target = E.FindTarget(Def.SenseRange, false);
		const ADCBuilding* Tower = nullptr;
		float Best = TNumericLimits<float>::Max();
		for (ADCBuilding* B : TActorRange<ADCBuilding>(E.GetWorld()))
		{
			const float D = B->IsCollapsed() ? Best : FVector::Dist2D(B->GetActorLocation(), E.GetActorLocation());
			Tower = D < Best ? B : Tower;
			Best = FMath::Min(Best, D);
		}
		const FVector Goal = Tower ? Tower->GetActorLocation() : (Target ? Target->GetActorLocation() : E.GetActorLocation() + E.Heading);
		const FVector Want = (Goal - E.GetActorLocation()).GetSafeNormal2D();
		if (!Want.IsNearlyZero())
		{
			E.Heading = FMath::VInterpNormalRotationTo(E.Heading, Want, Dt, 12.f);
		}
		const FVector Next = E.GetActorLocation() + E.Heading * Def.Speed * Dt;
		E.SetActorLocation(FVector(Next.X, Next.Y, Def.HoverHeight));
		E.SetActorRotation(E.Heading.Rotation());
		// A walker shoulders straight through towers: every tower it touches comes down.
		for (ADCBuilding* B : TActorRange<ADCBuilding>(E.GetWorld()))
		{
			const FDCBuildingSpec& S = B->GetSpec();
			const float Half = FMath::Max(S.SizeX, S.SizeY) * S.BlockSize * 0.5f;
			if (!B->IsCollapsed() && FVector::Dist2D(B->GetActorLocation(), E.GetActorLocation()) < Half + 250.f)
			{
				if (ADCGameMode* GM = E.GetWorld()->GetAuthGameMode<ADCGameMode>())
				{
					GM->ScheduleCollapse(B, E.GetActorLocation(), TEXT("Topple"), UDCTuning::Get().City.ToppleTelegraphSeconds);
				}
			}
		}
		if (Now >= E.NextCueTime)
		{
			E.NextCueTime = Now + 0.9f;
			E.Cue(TEXT("WalkerStomp"));
		}
		if (Target && FVector::Dist2D(Target->GetActorLocation(), E.GetActorLocation()) < Def.AttackRange)
		{
			E.TryAttack(Target, Now);
		}
	}

	using FBehaviour = void (*)(ADCEnemy&, const FDCEnemyDef&, float, float);
	const TMap<FName, FBehaviour>& Behaviours()
	{
		static const TMap<FName, FBehaviour> Map = {
			{Swarmer, &TickSwarmer},
			{Spotlight, &TickSpotlight},
			{Walker, &TickWalker},
		};
		return Map;
	}
}

ADCEnemy::ADCEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	Body->InitSphereRadius(45.f);
	Body->SetCollisionProfileName(TEXT("Pawn"));
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RootComponent = Body;

	auto MakePart = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* M = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		M->SetupAttachment(Body);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return M;
	};
	Visual = MakePart(TEXT("Visual"));
	Eye = MakePart(TEXT("Eye"));
	LegA = MakePart(TEXT("LegA"));
	LegB = MakePart(TEXT("LegB"));
	LegC = MakePart(TEXT("LegC"));

	Spot = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spot"));
	Spot->SetupAttachment(Body);
	Spot->SetVisibility(false);
	Spot->SetCastShadows(false);
}

void ADCEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCEnemy, Kind);
	DOREPLIFETIME(ADCEnemy, Health);
	DOREPLIFETIME(ADCEnemy, StunnedUntil);
	DOREPLIFETIME(ADCEnemy, LightTarget);
}

const FDCEnemyDef* ADCEnemy::GetDef() const
{
	return UDCTuning::Get().FindEnemy(Kind);
}

bool ADCEnemy::IsWalker() const
{
	const FDCEnemyDef* Def = GetDef();
	return Def && Def->Behaviour == Walker;
}

void ADCEnemy::BeginPlay()
{
	Super::BeginPlay();
	if (const FDCEnemyDef* Def = GetDef())
	{
		if (HasAuthority())
		{
			Health = Def->MaxHealth;
		}
		BuildVisual();
	}
	Heading = GetActorForwardVector();
}

void ADCEnemy::BuildVisual()
{
	const FDCEnemyDef& Def = *GetDef();
	const float S = Def.Scale;
	Visual->SetMaterial(0, DCAssets::MakeFlat(this, Def.Color));
	Eye->SetStaticMesh(DCAssets::Sphere());
	Eye->SetMaterial(0, DCAssets::MakeFlat(this, Def.EyeColor, 40.f));
	if (Def.Behaviour == Walker)
	{
		Body->SetSphereRadius(150.f * S);
		Visual->SetStaticMesh(DCAssets::Cube());
		Visual->SetRelativeScale3D(FVector(3.f, 2.4f, 1.6f) * S);
		Eye->SetRelativeLocation(FVector(150.f, 0, 20.f) * S);
		Eye->SetRelativeScale3D(FVector(0.8f) * S);
		int32 Index = 0;
		for (UStaticMeshComponent* Leg : {LegA.Get(), LegB.Get(), LegC.Get()})
		{
			const float Angle = Index++ * 120.f;
			const FVector Foot = FVector(160.f, 0, 0).RotateAngleAxis(Angle, FVector::UpVector) * S;
			Leg->SetStaticMesh(DCAssets::Cylinder());
			Leg->SetMaterial(0, DCAssets::MakeFlat(this, Def.Color * 1.5f));
			Leg->SetRelativeLocation(Foot - FVector(0, 0, Def.HoverHeight * 0.5f));
			Leg->SetRelativeScale3D(FVector(0.45f * S, 0.45f * S, Def.HoverHeight / 100.f));
		}
		return;
	}
	if (Def.Behaviour == Spotlight)
	{
		Body->SetSphereRadius(110.f * S);
		Visual->SetStaticMesh(DCAssets::Cylinder());
		Visual->SetRelativeScale3D(FVector(2.4f, 2.4f, 0.6f) * S);
		Eye->SetRelativeLocation(FVector(0, 0, -40.f) * S);
		Eye->SetRelativeScale3D(FVector(1.0f) * S);
		Eye->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(1.f, 0.95f, 0.8f), 60.f));
		Spot->SetVisibility(true);
		Spot->SetIntensity(400000.f);
		Spot->SetAttenuationRadius(Def.LightRange * 1.3f);
		Spot->SetOuterConeAngle(Def.LightHalfAngle);
		Spot->SetInnerConeAngle(Def.LightHalfAngle * 0.6f);
		Spot->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
		return;
	}
	Body->SetSphereRadius(45.f * S);
	Visual->SetStaticMesh(DCAssets::Sphere());
	Visual->SetRelativeScale3D(FVector(0.9f) * S);
	Eye->SetRelativeLocation(FVector(38.f, 0, 8.f) * S);
	Eye->SetRelativeScale3D(FVector(0.3f) * S);
	LegA->SetStaticMesh(DCAssets::Cone());
	LegA->SetMaterial(0, DCAssets::MakeFlat(this, Def.EyeColor * 0.6f));
	LegA->SetRelativeLocation(FVector(-40.f, 0, 0) * S);
	LegA->SetRelativeRotation(FRotator(90.f, 0, 0));
	LegA->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.6f) * S);
}

bool ADCEnemy::FindLure(float Range, FVector& Out) const
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS)
	{
		return false;
	}
	const float Now = GS->Now();
	for (const TPair<FVector, float>& Lure : GS->Lures)
	{
		if (Lure.Value > Now && FVector::Dist2D(Lure.Key, GetActorLocation()) < Range)
		{
			Out = Lure.Key;
			return true;
		}
	}
	return false;
}

ADCCharacter* ADCEnemy::FindTarget(float Range, bool bPreferPhotographers) const
{
	ADCCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const float Aggro = UDCTuning::Get().Camera.AggroRadiusCm;
	for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
	{
		if (C->IsDowned())
		{
			continue;
		}
		const float Dist = FVector::Dist(C->GetActorLocation(), GetActorLocation());
		const bool bPhotographer = bPreferPhotographers && C->IsCameraRaised() && Dist < Aggro;
		if (Dist > Range && !bPhotographer)
		{
			continue;
		}
		const float Score = bPhotographer ? Dist - 100000.f : Dist;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = C;
		}
	}
	return Best;
}

void ADCEnemy::SteerTowards(const FVector& Goal, float Speed, float DeltaSeconds, float Hover)
{
	const FVector Here = GetActorLocation();
	const float Ground = GroundHeightBelow(GetWorld(), Here, this);
	FVector To = Goal - Here;
	const float Dist2D = To.Size2D();
	// Cruise at hover height over whatever is below; dive onto the goal when close.
	const float CruiseZ = Ground + Hover;
	const float WantZ = Dist2D < 600.f ? Goal.Z : FMath::Max(CruiseZ, Goal.Z);
	// After hitting a wall it keeps climbing for a moment, so cruise height does not pull it back into the wall.
	To.Z = GetWorld()->GetTimeSeconds() < ClimbUntil ? Speed * DeltaSeconds * 4.f : WantZ - Here.Z;
	const FVector Step = To.GetClampedToMaxSize(Speed * DeltaSeconds);
	FHitResult Hit;
	SetActorLocation(Here + Step, true, &Hit);
	if (Hit.bStartPenetrating)
	{
		// Started inside geometry (rubble landed on it, or a tight spawn): rise out without sweeping.
		SetActorLocation(Here + FVector(Step.X, Step.Y, Speed * DeltaSeconds * 2.f), false);
	}
	else if (Hit.bBlockingHit && Cast<APawn>(Hit.GetActor()))
	{
		// Blocked by a packmate (or a player): slide round it. Without this a tight pack deadlocked for good.
		const FVector Slide = FVector::VectorPlaneProject(Step, Hit.ImpactNormal.GetSafeNormal2D());
		SetActorLocation(GetActorLocation() + Slide, true);
	}
	else if (Hit.bBlockingHit)
	{
		// Blocked by a tower: the first escape that moves wins - slide along it and climb, climb straight up,
		// or back off the wall and climb (inside corners and window ledges block the first two).
		const float Up = FMath::Max(Speed * DeltaSeconds, 2.f);
		const FVector Out = Hit.ImpactNormal.GetSafeNormal2D() * Up;
		const FVector Escapes[] = {FVector::VectorPlaneProject(Step, Hit.ImpactNormal.GetSafeNormal2D()) + FVector(0, 0, Up), FVector(0, 0, Up), Out + FVector(0, 0, Up), Out * 2.f};
		for (const FVector& Try : Escapes)
		{
			const FVector From = GetActorLocation();
			SetActorLocation(From + Try, true);
			if (!GetActorLocation().Equals(From, 0.1f))
			{
				break;
			}
		}
		ClimbUntil = GetWorld()->GetTimeSeconds() + 0.8f;
	}
	if (!Step.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Step.Rotation(), DeltaSeconds, 8.f));
	}
}

void ADCEnemy::TryAttack(ADCCharacter* Target, float Now)
{
	const FDCEnemyDef* Def = GetDef();
	if (!Def || Now < NextAttackTime || FVector::Dist(Target->GetActorLocation(), GetActorLocation()) > Def->AttackRange)
	{
		return;
	}
	NextAttackTime = Now + Def->AttackInterval;
	Target->HostApplyHurt(Def->Damage, GetActorLocation());
}

void ADCEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FDCEnemyDef* Def = GetDef();
	if (!Def)
	{
		return;
	}
	if (Spot->IsVisible())
	{
		Spot->SetWorldRotation((FVector(LightTarget) - GetActorLocation()).Rotation());
	}
	if (!IsAlive())
	{
		// Walkers topple forward; others drop.
		if (Def->Behaviour == Walker)
		{
			AddActorLocalRotation(FRotator(-70.f * DeltaSeconds, 0, 0));
		}
		return;
	}
	if (!HasAuthority())
	{
		return;
	}
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const float Now = GS ? GS->Now() : GetWorld()->GetTimeSeconds();
	if (Now < StunnedUntil)
	{
		// Stunned by a strobe: drift and spin in place, easy prey for a photo or a rifle.
		AddActorWorldRotation(FRotator(0, 400.f * DeltaSeconds, 0));
		return;
	}
	const FVector Before = GetActorLocation();
	if (const FBehaviour* Behaviour = Behaviours().Find(Def->Behaviour))
	{
		(*Behaviour)(*this, *Def, DeltaSeconds, GS ? GS->Now() : GetWorld()->GetTimeSeconds());
	}
	// Evidence trace (-dcenemytrace): every 5 s, where each enemy is and whether it moved.
	if (FParse::Param(FCommandLine::Get(), TEXT("dcenemytrace")) && GetWorld()->GetTimeSeconds() >= NextTraceTime)
	{
		NextTraceTime = GetWorld()->GetTimeSeconds() + 5.f;
		UE_LOG(LogTemp, Display, TEXT("DCTRACE %s %s at %s moved=%.1f dt=%.3f rushing=%d"), *GetName(), *Def->Id.ToString(), *GetActorLocation().ToCompactString(),
			FVector::Dist(Before, GetActorLocation()), DeltaSeconds, bRushing ? 1 : 0);
	}
}

float ADCEnemy::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !IsAlive())
	{
		return 0.f;
	}
	const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Max(0.f, Health - Damage);
	if (Health <= 0.f)
	{
		Die(EventInstigator);
	}
	return Applied > 0.f ? Applied : Damage;
}

void ADCEnemy::Die(AController* Killer)
{
	const FDCEnemyDef* Def = GetDef();
	if (Killer)
	{
		if (ADCPlayerState* PS = Killer->GetPlayerState<ADCPlayerState>())
		{
			PS->AddPoints(Def ? Def->KillPoints : UDCTuning::Get().Economy.KillPoints);
			++PS->Kills;
		}
	}
	SetActorEnableCollision(false);
	if (Def && Def->Behaviour == Walker)
	{
		// The topple: whatever tower it falls on comes down (a chain worth photographing).
		const FVector FallPoint = GetActorLocation() + Heading * 1500.f;
		for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
		{
			if (FVector::Dist2D(B->GetActorLocation(), FallPoint) < 2200.f)
			{
				if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
				{
					GM->ScheduleCollapse(B, FallPoint, TEXT("Topple"), UDCTuning::Get().City.ToppleTelegraphSeconds);
				}
			}
		}
	}
	PlayDeathFx();
	SetLifeSpan(Def && Def->Behaviour == Walker ? 2.5f : 0.3f);
}

void ADCEnemy::OnRep_Health()
{
	if (!IsAlive())
	{
		PlayDeathFx();
	}
}

void ADCEnemy::PlayDeathFx()
{
	if (bDeathFxPlayed)
	{
		return;
	}
	bDeathFxPlayed = true;
	Spot->SetVisibility(false);
	DCAudio::PlayAt(this, TEXT("Kill"), GetActorLocation(), 0.4f, 0.7f);
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr;
	const FDCEnemyDef* Def = GetDef();
	if (!Pool || !Def)
	{
		return;
	}
	for (int32 i = 0; i < 6; ++i)
	{
		FDCDebrisSpawn D;
		D.Transform = FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), GetActorLocation(), FVector(0.2f * Def->Scale));
		D.Color = i % 2 ? Def->EyeColor : Def->Color;
		D.Glow = i % 2 ? 20.f : 0.f;
		D.LinearVelocity = FMath::VRand() * 600.f + FVector(0, 0, 400);
		D.LifeSeconds = 2.f;
		Pool->Spawn(D);
	}
}

void ADCEnemy::MulticastCue_Implementation(FName Cue)
{
	DCAudio::PlayAt(this, Cue, GetActorLocation());
}
