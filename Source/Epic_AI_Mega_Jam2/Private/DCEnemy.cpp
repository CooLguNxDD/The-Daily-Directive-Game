#include "DCEnemy.h"
#include "DCFrameScopes.h"
#include "DCUnits.h"
#include "DCVoxelFx.h"
#include "Engine/DamageEvents.h"
#include "Rules/DCFactionRules.h"

#include "DCVoxelCity.h"

#include "ProceduralMeshComponent.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"
#include "DCUnitAnimator.h"
#include "TimerManager.h"

#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PointLightComponent.h"
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
		if (!Target)
		{
			// R3: no crew in reach: a hostile faction's unit in sense range comes before hunting the crew (fights the
			// player did not start).
			if (Now >= E.NextRetarget)
			{
				E.NextRetarget = Now + 0.25f;
				E.Foe = E.FindFoe(Def.SenseRange);
			}
			if (AActor* Foe = E.Foe.Get(); Foe && !Cast<ADCCharacter>(Foe) && Cast<ADCEnemy>(Foe)->IsAlive())
			{
				E.SteerTowards(Foe->GetActorLocation() + FVector(0, 0, 60.f), Def.Speed, Dt, Def.HoverHeight);
				E.HostAttack(Foe, Now);
				return;
			}
		}
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
		const ADCVoxelCity* City = ADCVoxelCity::Get(&E);
		E.SetActorLocation(FVector(Next.X, Next.Y, (City ? City->SurfaceZCm(Next) : 0.f) + Def.HoverHeight)); // stands on the street and its rubble
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

	bool IsLiveFoe(const AActor* A)
	{
		if (const ADCCharacter* C = Cast<ADCCharacter>(A))
		{
			return !C->IsDowned();
		}
		const ADCEnemy* U = Cast<ADCEnemy>(A);
		return U && U->IsAlive();
	}

	// Squad goals without a foe in sight (R3), by the row's Goal id. True when a goal was found.
	// Crew grace (advisor a-10, FactionMix.CrewGraceSeconds): early in a sector units leave the crew alone unless
	// it comes within CrewSelfDefenceCm. One rule for the opening clash, nests, hunters and early raiders.
	// Unit damage to a crew member, scaled by crew size (FactionMix.CrewDamageByPlayers).
	float CrewDamage(const UWorld* World, float Base)
	{
		const TArray<float>& Scale = UDCTuning::Get().FactionMix.CrewDamageByPlayers;
		const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
		const int32 Players = GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
		return Scale.Num() > 0 ? Base * Scale[FMath::Min(Players, Scale.Num()) - 1] : Base;
	}

	bool InCrewGrace(const UWorld* World)
	{
		const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
		return GS && GS->Phase == EDCRunPhase::Sector && GS->Now() - GS->SectorStartTime < UDCTuning::Get().FactionMix.CrewGraceSeconds;
	}

	// R6: a foe inside a standing tower that the unit is not in yet: walk to the tower's nearest door or breach first
	// (a step past it, inside), then straight at the foe. Units of factions that stay out keep the direct goal.
	FVector EntryGoal(const ADCEnemy& E, const FVector& Here, const FVector& Goal)
	{
		const ADCVoxelCity* City = ADCVoxelCity::Get(&E);
		const FDCFactionDef* F = DCFactionRules::Find(UDCTuning::Get().Factions, E.GetFaction());
		const int32 GoalLot = City && F && F->bEntersBuildings ? City->StandingLotAt(Goal) : INDEX_NONE;
		FVector Door;
		if (GoalLot == INDEX_NONE || City->StandingLotAt(Here) == GoalLot || !City->NearestPortal(GoalLot, Here, Door))
		{
			return Goal;
		}
		return FVector::Dist2D(Here, Door) > 150.f ? Door : Door + (Goal - Door).GetSafeNormal2D() * 200.f;
	}

	bool FoeInOtherTower(const ADCEnemy& E, const FVector& Here, const FVector& At)
	{
		const ADCVoxelCity* City = ADCVoxelCity::Get(&E);
		const FDCFactionDef* F = DCFactionRules::Find(UDCTuning::Get().Factions, E.GetFaction());
		const int32 FoeLot = City && F && F->bEntersBuildings ? City->StandingLotAt(At) : INDEX_NONE;
		return FoeLot != INDEX_NONE && City->StandingLotAt(Here) != FoeLot;
	}

	using FGoalFn = bool (*)(const ADCEnemy&, const FDCEnemyDef&, float, FVector&);
	bool GoalPlayers(const ADCEnemy& E, const FDCEnemyDef& Def, float Now, FVector& Out)
	{
		if (InCrewGrace(E.GetWorld()))
		{
			return false;
		}
		const ADCCharacter* C = E.FindTarget(Def.HuntRangeCm > 0.f ? Def.HuntRangeCm : 1e7f, false);
		Out = C ? C->GetActorLocation() : Out;
		return C != nullptr;
	}
	bool GoalRubble(const ADCEnemy& E, const FDCEnemyDef& Def, float Now, FVector& Out)
	{
		// Looters (pirates): the newest collapse on the server clock, then the crew.
		const ADCGameState* GS = E.GetWorld()->GetGameState<ADCGameState>();
		for (int32 i = GS ? GS->Collapses.Num() - 1 : -1; i >= 0; --i)
		{
			if (GS->Collapses[i].Time <= Now)
			{
				Out = GS->Collapses[i].Epicenter;
				return true;
			}
		}
		return GoalPlayers(E, Def, Now, Out);
	}
	bool GoalFront(const ADCEnemy& E, const FDCEnemyDef& Def, float Now, FVector& Out)
	{
		// The friendly army pushes on the nearest hostile unit anywhere; with none, it walks with the crew.
		const UDCUnits* Units = UDCUnits::Get(&E);
		const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
		float Best = TNumericLimits<float>::Max();
		if (Units)
		{
			for (const ADCEnemy* U : Units->All())
			{
				if (U && U != &E && U->IsAlive() && DCFactionRules::IsHostile(Factions, E.GetFaction(), U->GetFaction()))
				{
					const float D = FVector::DistSquared2D(U->GetActorLocation(), E.GetActorLocation());
					Out = D < Best ? U->GetActorLocation() : Out;
					Best = FMath::Min(Best, D);
				}
			}
		}
		return Best < TNumericLimits<float>::Max() || GoalPlayers(E, Def, Now, Out);
	}
	const TMap<FName, FGoalFn>& Goals()
	{
		static const TMap<FName, FGoalFn> Map = {
			{TEXT("Players"), &GoalPlayers},
			{TEXT("Rubble"), &GoalRubble},
			{TEXT("Front"), &GoalFront},
		};
		return Map;
	}

	// Formation slot offsets (XY, cm) from the squad's goal, by the row's Formation id.
	using FFormationFn = FVector (*)(int32 Slot, int32 Size, const FVector& Facing, float Spacing, uint32 Hash);
	FVector FormCrowd(int32 Slot, int32 Size, const FVector& Facing, float Spacing, uint32 Hash)
	{
		const float Angle = (Hash % 3600) * 0.1f;
		const float R = Spacing * FMath::Sqrt(static_cast<float>(Size)) * ((Hash >> 12) % 1000) / 1000.f;
		return FVector(R, 0.f, 0.f).RotateAngleAxis(Angle, FVector::UpVector);
	}
	FVector FormLine(int32 Slot, int32 Size, const FVector& Facing, float Spacing, uint32 Hash)
	{
		const FVector Right(-Facing.Y, Facing.X, 0.f);
		return Right * (Slot - (Size - 1) * 0.5f) * Spacing;
	}
	FVector FormSkirmish(int32 Slot, int32 Size, const FVector& Facing, float Spacing, uint32 Hash)
	{
		const FVector Right(-Facing.Y, Facing.X, 0.f);
		return Right * (Slot - (Size - 1) * 0.5f) * Spacing * 1.8f - Facing * ((Slot % 2) * Spacing);
	}
	const TMap<FName, FFormationFn>& Formations()
	{
		static const TMap<FName, FFormationFn> Map = {
			{TEXT("Crowd"), &FormCrowd},
			{TEXT("Line"), &FormLine},
			{TEXT("Skirmish"), &FormSkirmish},
		};
		return Map;
	}

	void TickInfantry(ADCEnemy& E, const FDCEnemyDef& Def, float Dt, float Now)
	{
		// R3 ground unit: fight the nearest foe in sense range (crew or a hostile faction), else follow the squad goal.
		if (!Def.IdleCue.IsNone() && Now >= E.NextCueTime)
		{
			E.NextCueTime = Now + Def.IdleCueSeconds * (0.6f + 0.8f * ((E.GetUniqueID() * 2654435761u) % 1000) / 1000.f);
			E.Cue(Def.IdleCue);
		}
		if (Now >= E.NextRetarget || !IsLiveFoe(E.Foe.Get()))
		{
			E.NextRetarget = Now + 0.25f + (E.GetUniqueID() % 8) * 0.01f; // ~4 Hz, staggered (advisor a-2)
			E.Foe = E.FindFoe(Def.SenseRange);
		}
		const FVector Here = E.GetActorLocation();
		if (AActor* Foe = IsLiveFoe(E.Foe.Get()) ? E.Foe.Get() : nullptr)
		{
			const FVector At = Foe->GetActorLocation();
			const FVector To = (At - Here).GetSafeNormal2D();
			const float Dist = FVector::Dist2D(At, Here);
			// R6: a foe inside a standing tower this unit is not in yet is out of sight behind its walls: shooters do not
			// hold at range there, they keep walking door-first until they share the tower (checked only when in range).
			const float RangeHold = FMath::Max((Def.ShotRange > 0.f ? Def.ShotRange : Def.AttackRange) * 0.6f, E.CrewContactCm());
			const float Hold = Dist > RangeHold || !FoeInOtherTower(E, Here, At) ? RangeHold : E.CrewContactCm();
			if (Def.KiteCm > 0.f && Dist < Def.KiteCm && !FoeInOtherTower(E, Here, At))
			{
				// Hit and run (Scavengers): back off at speed, still firing.
				E.GroundMove(Here - To * 600.f, Def.Speed, Dt, Def.HoverHeight);
				E.SetActorRotation(FMath::RInterpTo(E.GetActorRotation(), To.Rotation(), Dt, 8.f));
			}
			else if (Dist > Hold)
			{
				const float Speed = Def.Speed * (Def.ShotRange > 0.f ? 1.f : Def.RushMultiplier);
				E.GroundMove(EntryGoal(E, Here, At + E.SlotOffset(Def, To) * 0.3f), Speed, Dt, Def.HoverHeight);
			}
			else if (!To.IsNearlyZero())
			{
				E.SetActorRotation(FMath::RInterpTo(E.GetActorRotation(), To.Rotation(), Dt, 8.f));
			}
			E.HostAttack(Foe, Now);
			return;
		}
		FVector Goal = Here + E.Heading * 800.f;
		const FGoalFn* Fn = Goals().Find(Def.Goal);
		if (Fn && (*Fn)(E, Def, Now, Goal))
		{
			if (!Def.GoalActCue.IsNone() && FVector::Dist2D(Goal, Here) < Def.GoalActRadiusCm)
			{
				// At the goal: act there (loot) instead of milling about.
				if (Now >= E.NextAttackTime)
				{
					E.NextAttackTime = Now + Def.GoalActSeconds;
					E.Cue(Def.GoalActCue);
				}
				return;
			}
			const FVector Facing = (Goal - Here).GetSafeNormal2D();
			E.GroundMove(EntryGoal(E, Here, Goal + E.SlotOffset(Def, Facing.IsNearlyZero() ? E.Heading : Facing)), Def.Speed * 0.7f, Dt, Def.HoverHeight); // R6: door first
			return;
		}
		E.Heading = E.Heading.RotateAngleAxis(15.f * Dt, FVector::UpVector);
		E.GroundMove(Here + E.Heading * 600.f, Def.Speed * 0.35f, Dt, Def.HoverHeight);
	}

	void TickNest(ADCEnemy& E, const FDCEnemyDef& Def, float Dt, float Now)
	{
		// R3 Hive nest: stays put, pulses its faction out while it stands.
		if (Def.SpawnEverySeconds <= 0.f || Def.SpawnCount <= 0 || Now < E.NextAttackTime)
		{
			return;
		}
		E.NextAttackTime = Now + Def.SpawnEverySeconds;
		const UDCUnits* Units = UDCUnits::Get(&E);
		ADCGameMode* GM = E.GetWorld()->GetAuthGameMode<ADCGameMode>();
		if (GM && Units && Units->CountAlive(E.GetFaction()) < Def.SpawnMaxAlive)
		{
			GM->HostSpawnSquad(E.GetFaction(), Def.SpawnCount, E.GetActorLocation(), 250.f);
			E.Cue(TEXT("Groan"));
		}
	}

	using FBehaviour = void (*)(ADCEnemy&, const FDCEnemyDef&, float, float);
	const TMap<FName, FBehaviour>& Behaviours()
	{
		static const TMap<FName, FBehaviour> Map = {
			{Swarmer, &TickSwarmer},
			{Spotlight, &TickSpotlight},
			{Walker, &TickWalker},
			{TEXT("Infantry"), &TickInfantry},
			{TEXT("Nest"), &TickNest},
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
	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(Body);
	GlowLight->SetVisibility(false);
	GlowLight->SetCastShadows(false);
}

void ADCEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCEnemy, Kind);
	DOREPLIFETIME_CONDITION(ADCEnemy, SpawnServerTime, COND_InitialOnly);
	DOREPLIFETIME(ADCEnemy, Health);
	DOREPLIFETIME(ADCEnemy, StunnedUntil);
	DOREPLIFETIME(ADCEnemy, LightTarget);
	DOREPLIFETIME(ADCEnemy, AttackSeq);
}

UDCUnits* UDCUnits::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDCUnits>() : nullptr;
}

int32 UDCUnits::CountAlive(FName Faction) const
{
	int32 N = 0;
	for (const ADCEnemy* U : Units)
	{
		N += U && U->IsAlive() && U->GetFaction() == Faction ? 1 : 0;
	}
	return N;
}

const ADCEnemy* UDCUnits::InClash(const ADCEnemy& Unit, float RangeCm) const
{
	const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
	for (const ADCEnemy* U : Units)
	{
		if (U && U != &Unit && U->IsAlive() && FVector::DistSquared(U->GetActorLocation(), Unit.GetActorLocation()) < RangeCm * RangeCm
			&& DCFactionRules::IsHostile(Factions, Unit.GetFaction(), U->GetFaction()))
		{
			return U;
		}
	}
	return nullptr;
}

int32 UDCUnits::SlotCap(bool bRanged) const
{
	const FDCFactionMixRule& Mix = UDCTuning::Get().FactionMix;
	const TArray<int32>& Caps = bRanged ? Mix.CrewRangedSlotsByPlayers : Mix.CrewMeleeSlotsByPlayers;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const int32 Players = GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
	return Caps.Num() > 0 ? Caps[FMath::Min(Players, Caps.Num()) - 1] : MAX_int32;
}

bool UDCUnits::TryEngage(const AActor& Victim, const ADCEnemy& Unit, bool bRanged, float Now)
{
	TArray<FDCAttackSlot>& Slots = (bRanged ? RangedSlots : MeleeSlots).FindOrAdd(Victim.GetUniqueID());
	return DCFactionRules::GrantSlot(Slots, Unit.GetUniqueID(), Now, SlotCap(bRanged), UDCTuning::Get().FactionMix.CrewSlotLeaseSeconds);
}

float UDCUnits::SpendCrewDamage(const AActor& Victim, float Amount, float Now)
{
	const FDCFactionMixRule& Mix = UDCTuning::Get().FactionMix;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const int32 Players = GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
	const float Dps = Mix.CrewDpsByPlayers.Num() > 0 ? Mix.CrewDpsByPlayers[FMath::Min(Players, Mix.CrewDpsByPlayers.Num()) - 1] : 0.f;
	return DCFactionRules::SpendDamage(DamageBuckets.FindOrAdd(Victim.GetUniqueID()), Amount, Now, Dps, Mix.CrewBurstHealth);
}

bool UDCUnits::CanEngage(const AActor& Victim, const ADCEnemy& Unit, bool bRanged, float Now) const
{
	const TArray<FDCAttackSlot>* Slots = (bRanged ? RangedSlots : MeleeSlots).Find(Victim.GetUniqueID());
	return !Slots || DCFactionRules::HasSlot(*Slots, Unit.GetUniqueID(), Now, SlotCap(bRanged));
}

FName ADCEnemy::GetFaction() const
{
	const FDCEnemyDef* Def = GetDef();
	return Def ? Def->Faction : NAME_None;
}

FVector ADCEnemy::SlotOffset(const FDCEnemyDef& Def, const FVector& Facing) const
{
	const FFormationFn* Fn = Formations().Find(Def.Formation);
	const uint32 Hash = HashCombine(GetTypeHash(Squad), GetTypeHash(Slot * 7919 + 1));
	return Fn ? (*Fn)(Slot, SquadSize, Facing.GetSafeNormal2D(), Def.FormationSpacingCm, Hash) : FVector::ZeroVector;
}

AActor* ADCEnemy::FindFoe(float Range) const
{
	const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
	const FName Mine = GetFaction();
	const FVector Here = GetActorLocation();
	AActor* Best = nullptr;
	float BestD = Range * Range;
	if (Mine.IsNone() || DCFactionRules::FightsCrew(Factions, Mine))
	{
		const float CrewD = InCrewGrace(GetWorld()) ? FMath::Min(BestD, FMath::Square(UDCTuning::Get().FactionMix.CrewSelfDefenceCm)) : BestD;
		// A crew member whose attack slots are all held counts as out of range (advisor a-17): surplus units fight rivals.
		const UDCUnits* SlotOwner = UDCUnits::Get(this);
		const ADCGameState* SlotGS = GetWorld()->GetGameState<ADCGameState>();
		const FDCEnemyDef* MyDef = GetDef();
		const bool bRangedMe = MyDef && MyDef->ShotRange > 0.f;
		for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
		{
			const float Dist = FVector::DistSquared(C->GetActorLocation(), Here);
			const bool bSaturated = SlotOwner && SlotGS && !SlotOwner->CanEngage(*C, *this, bRangedMe, SlotGS->Now());
			const float D = C->IsDowned() || bSaturated || Dist >= CrewD ? BestD : Dist;
			Best = D < BestD ? C : Best;
			BestD = FMath::Min(BestD, D);
		}
	}
	const UDCUnits* Units = UDCUnits::Get(this);
	if (Mine.IsNone() || !Units)
	{
		return Best;
	}
	for (ADCEnemy* U : Units->All())
	{
		if (!U || U == this || !U->IsAlive() || !DCFactionRules::IsHostile(Factions, Mine, U->GetFaction()))
		{
			continue;
		}
		const float D = FVector::DistSquared(U->GetActorLocation(), Here);
		Best = D < BestD ? U : Best;
		BestD = FMath::Min(BestD, D);
	}
	return Best;
}

void ADCEnemy::GroundMove(const FVector& Goal, float Speed, float DeltaSeconds, float Hover)
{
	const FVector Here = GetActorLocation();
	FVector To = Goal - Here;
	To.Z = 0.f;
	if (To.SizeSquared() < 60.f * 60.f)
	{
		return;
	}
	// Separation: a light 2D push off packmates, so a crowd spreads instead of stacking.
	FVector Push = FVector::ZeroVector;
	if (const UDCUnits* Units = UDCUnits::Get(this))
	{
		for (const ADCEnemy* U : Units->All())
		{
			FVector D = U && U != this ? Here - U->GetActorLocation() : FVector::ZeroVector;
			D.Z = 0.f;
			const float Dist = D.Size();
			Push += Dist > 1.f && Dist < 160.f ? D / Dist * (160.f - Dist) / 160.f : FVector::ZeroVector;
		}
	}
	const FVector Want = (To.GetSafeNormal() + Push * 0.8f).GetSafeNormal2D();
	// Crew contact ring (advisor a-10): unswept moves never step inside body + capsule + margin of a standing player
	// (stepping out of it is always allowed), so a crowd rings the player instead of pinning the capsule.
	TArray<FVector, TInlineAllocator<4>> Crew;
	for (const ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
	{
		if (!C->IsDowned())
		{
			Crew.Add(C->GetActorLocation());
		}
	}
	const float Contact = CrewContactCm();
	auto IntoCrew = [&Crew, &Here, Contact](const FVector& Next)
	{
		for (const FVector& At : Crew)
		{
			const float D = FVector::Dist2D(Next, At);
			if (D < Contact && D < FVector::Dist2D(Here, At))
			{
				return true;
			}
		}
		return false;
	};
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	const float GroundHere = City ? City->SurfaceZCm(Here) : 0.f;
	// R6: factions that enter buildings step by the walk mask (doors, breaches) instead of a tower's solid box.
	const FDCFactionDef* MyFaction = DCFactionRules::Find(UDCTuning::Get().Factions, GetFaction());
	const bool bEnters = MyFaction && MyFaction->bEntersBuildings;
	const float Step = Speed * DeltaSeconds;
	// The first heading that does not walk into a wall wins: straight, then fanning out both ways (climbs rubble,
	// never a tower face: a column top more than a step above is a wall).
	static const float Fan[] = {0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f};
	for (const float Deg : Fan)
	{
		const FVector Dir = Want.RotateAngleAxis(Deg, FVector::UpVector);
		const FVector Next = Here + Dir * Step;
		const FVector Probe = Here + Dir * FMath::Max(Step, 120.f);
		const float Ground = City ? City->SurfaceZCm(Probe) : 0.f;
		const FVector BodyAt(Probe.X, Probe.Y, Ground + 120.f);
		if (IntoCrew(Next) || Ground - GroundHere > 160.f || (City && !City->IsInsideBounds(BodyAt)) || (City && (bEnters ? City->IsWalkBlocked(BodyAt) : City->IsSolidAt(BodyAt))))
		{
			continue;
		}
		const float NextGround = City ? City->SurfaceZCm(Next) : 0.f;
		const float Z = FMath::FInterpTo(Here.Z, NextGround + Hover, DeltaSeconds, 12.f);
		SetActorLocation(FVector(Next.X, Next.Y, Z));
		Heading = Dir;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Dir.Rotation(), DeltaSeconds, 8.f));
		return;
	}
	Heading = Heading.RotateAngleAxis(90.f, FVector::UpVector); // boxed in: turn and try another way next tick
}

float ADCEnemy::CrewContactCm() const
{
	// Body radius + the player capsule radius + data margin (the player capsule radius from the class default).
	const ADCCharacter* Cdo = GetDefault<ADCCharacter>();
	const float Capsule = Cdo && Cdo->GetCapsuleComponent() ? Cdo->GetCapsuleComponent()->GetUnscaledCapsuleRadius() : 42.f;
	return (Body ? Body->GetScaledSphereRadius() : 45.f) + Capsule + UDCTuning::Get().FactionMix.CrewContactMarginCm;
}

void ADCEnemy::HostAttack(AActor* Victim, float Now)
{
	const FDCEnemyDef* Def = GetDef();
	if (!Def || !Victim || Now < NextAttackTime)
	{
		return;
	}
	const FVector From = GetActorLocation() + FVector(0, 0, 40.f);
	const FVector At = Victim->GetActorLocation();
	const float Dist = FVector::Dist(From, At);
	const bool bRanged = Def->ShotRange > 0.f;
	// Melee reaches at least past the crew contact ring (units hold there and must still land their hits).
	const float Reach = bRanged ? Def->ShotRange : FMath::Max(Def->AttackRange, CrewContactCm() + 40.f);
	if (Dist > Reach)
	{
		return;
	}
	// Attack slots (advisor a-17): a crew member takes hits from a few units at a time; the rest wait their turn.
	UDCUnits* SlotOwner = Cast<ADCCharacter>(Victim) ? UDCUnits::Get(this) : nullptr;
	if (SlotOwner && !SlotOwner->TryEngage(*Victim, *this, bRanged, Now))
	{
		NextAttackTime = Now + 0.25f;
		return;
	}
	if (bRanged)
	{
		// Shots need a clear line through the voxel city (host-only, at the attack rate, not per tick).
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		// R6: a shooter in the same building as its target sees through that building's own walls box.
		const int32 SameLot = City ? City->StandingLotAt(At) : INDEX_NONE;
		const TArray<int32> Ignore = SameLot != INDEX_NONE && City->StandingLotAt(From) == SameLot ? TArray<int32>{SameLot} : TArray<int32>();
		if (City && City->IsSightBlocked(From, At, Ignore))
		{
			NextAttackTime = Now + 0.3f;
			return;
		}
	}
	NextAttackTime = Now + Def->AttackInterval;
	++AttackSeq; // R20: every machine plays the attack clip
	// Spread: the miss chance grows with range (ShotSpread degrees per 10 m).
	const float MissDeg = Def->ShotSpread * Dist / 1000.f;
	const bool bHit = !bRanged || FMath::FRand() > FMath::Clamp(MissDeg / 30.f, 0.f, 0.7f);
	if (bRanged)
	{
		const FVector Miss = bHit ? FVector::ZeroVector : FMath::VRand() * FMath::Min(Dist * 0.08f, 300.f);
		MulticastShot(From, At + Miss);
	}
	if (!bHit)
	{
		return;
	}
	if (ADCCharacter* C = Cast<ADCCharacter>(Victim))
	{
		UDCUnits* Budget = UDCUnits::Get(this);
		const float Hurt = CrewDamage(GetWorld(), Def->Damage);
		C->HostApplyHurt(Budget ? Budget->SpendCrewDamage(*C, Hurt, Now) : Hurt, GetActorLocation());
		return;
	}
	Victim->TakeDamage(Def->Damage, FDamageEvent(), nullptr, this);
}

void ADCEnemy::MulticastShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize To)
{
	const FDCFactionDef* F = DCFactionRules::Find(UDCTuning::Get().Factions, GetFaction());
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	if (ADCVoxelFx* Fx = City ? City->GetFx() : nullptr)
	{
		Fx->Tracer(From, To, F ? F->Cue : FLinearColor::White, UDCVoxelSettings::Get().Tuning.TracerGlow);
	}
	DCAudio::PlayAt(this, TEXT("FactionShot"), From, 0.35f, FMath::FRandRange(0.9f, 1.15f));
}

void ADCEnemy::TickGait(float DeltaSeconds)
{
	const FDCEnemyDef* Def = GetDef();
	if (Anim && ModelComp && Def && DeltaSeconds > 0.f)
	{
		// R20 (h-anim): the animator poses the segments; the model component takes the root's move about the feet.
		Anim->Advance(DeltaSeconds, GetActorLocation(), GetActorRotation().Yaw, Health, AttackSeq, IsAlive());
		Anim->ApplyRoot(*Def);
	}
	else if (!ModelComp || !Def || (Def->BobCm <= 0.f && Def->SwayDeg <= 0.f) || DeltaSeconds <= 0.f)
	{
		return;
	}
	else
	{
		TickBob(*Def, DeltaSeconds);
	}
	if (HasAuthority())
	{
		return;
	}
	// Clients (advisor a-3): infantry replicates at 15-20 Hz, so the model eases toward the replicated root instead of
	// stepping with each update; a big jump (spawn, teleport) snaps.
	const FVector Want = ModelComp->GetComponentLocation();
	SmoothModel = SmoothModel.IsZero() || FVector::DistSquared(SmoothModel, Want) > 500.f * 500.f ? Want : FMath::VInterpTo(SmoothModel, Want, DeltaSeconds, 18.f);
	ModelComp->SetWorldLocation(SmoothModel);
}

void ADCEnemy::TickBob(const FDCEnemyDef& InDef, float DeltaSeconds)
{
	const FDCEnemyDef* Def = &InDef;
	const FVector Here = GetActorLocation();
	const bool bMoving = FVector::Dist2D(Here, GaitLast) / DeltaSeconds > 30.f;
	GaitLast = Here;
	GaitPhase += bMoving ? DeltaSeconds * Def->StepHz * PI : 0.f;
	const float Bob = bMoving ? FMath::Abs(FMath::Sin(GaitPhase)) * Def->BobCm : 0.f;
	const float Sway = bMoving ? FMath::Sin(GaitPhase) * Def->SwayDeg : 0.f;
	ModelComp->SetRelativeLocation(Def->ModelOffset + FVector(0, 0, Bob));
	ModelComp->SetRelativeRotation(FRotator(0.f, 0.f, Sway));
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
	if (HasAuthority() && GetWorld() && GetWorld()->GetGameState())
	{
		SpawnServerTime = static_cast<float>(GetWorld()->GetGameState()->GetServerWorldTimeSeconds());
	}
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
	if (UDCUnits* Units = UDCUnits::Get(this))
	{
		Units->Add(this);
	}
	if (const FDCEnemyDef* Def = GetDef())
	{
		SetNetUpdateFrequency(Def->NetHz);
	}
	GaitLast = GetActorLocation();
	const FDCFactionDef* F = DCFactionRules::Find(UDCTuning::Get().Factions, GetFaction());
	const FDCEnemyDef* CueDef = GetDef();
	const int32 CueEvery = F ? FMath::RoundToInt32(F->CueLightEvery / ADCEnvironment::EffectScaleOf(this)) : 0; // rarer on low tiers
	if (F && CueDef && CueEvery > 0 && CueDef->GlowLight <= 0.f && GetUniqueID() % CueEvery == 0)
	{
		GlowLight->SetVisibility(true);
		GlowLight->SetIntensityUnits(ELightUnits::Candelas);
		GlowLight->SetIntensity(F->CueLightCandela);
		GlowLight->SetAttenuationRadius(F->CueLightRadiusCm);
		GlowLight->SetLightColor(F->Cue);
		GlowLight->SetRelativeLocation(FVector(30.f, 0.f, 60.f));
	}
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	if (ADCVoxelFx* Fx = F && !F->SpawnFx.IsNone() && City ? City->GetFx() : nullptr)
	{
		Fx->Emit(F->SpawnFx, FVector(GetActorLocation().X, GetActorLocation().Y, City->SurfaceZCm(GetActorLocation())), 150.f);
	}
}

void ADCEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Reason == EEndPlayReason::Destroyed && bDeathFxPlayed && !bBodyBurst)
	{
		BurstBody(); // R20: destroyed before the death clip ended (a client a little behind the host)
	}
	if (UDCUnits* Units = UDCUnits::Get(this))
	{
		Units->Remove(this);
	}
	Super::EndPlay(Reason);
}

bool ADCEnemy::BuildModel(const FDCEnemyDef& Def)
{
	DC_FRAME_SCOPE("EnemyBuild");
	const FDCVoxelMeshData* Mesh = DCVoxelModels::Mesh(Def.Model);
	if (!Mesh)
	{
		return false;
	}
	UProceduralMeshComponent* Model = NewObject<UProceduralMeshComponent>(this, TEXT("VoxelModel"));
	Model->SetupAttachment(Body);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->RegisterComponent();
	Model->SetRelativeLocation(Def.ModelOffset);
	Model->SetRelativeScale3D(FVector(Def.ModelScale));
	const AGameStateBase* SpawnGS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float SpawnAge = SpawnGS && SpawnServerTime >= 0.f ? static_cast<float>(SpawnGS->GetServerWorldTimeSeconds()) - SpawnServerTime : 0.f;
	Anim = UDCUnitAnimator::Attach(*this, *Model, Def, SpawnAge); // R20: a unit with a gait gets its segmented mesh there
	if (!Anim)
	{
		Model->CreateMeshSection(0, Mesh->Positions, Mesh->Indices, Mesh->Normals, Mesh->UVs, Mesh->Colors, {}, false);
		Model->SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
	}
	ModelComp = Model;
	for (UStaticMeshComponent* Part : {Visual.Get(), Eye.Get(), LegA.Get(), LegB.Get(), LegC.Get()})
	{
		Part->SetVisibility(false);
	}
	return true;
}

void ADCEnemy::BuildVisual()
{
	const FDCEnemyDef& Def = *GetDef();
	const float S = Def.Scale;
	BuildModel(Def); // the basic shapes below stay hidden behind a model, as the fallback, and set collision sizes
	// The eye glow (data): a small red light so a dark machine reads in the dark street.
	GlowLight->SetVisibility(Def.GlowLight > 0.f);
	GlowLight->SetIntensity(Def.GlowLight);
	GlowLight->SetAttenuationRadius(Def.GlowLightRadiusCm);
	GlowLight->SetLightColor(Def.EyeColor);
	GlowLight->SetRelativeLocation(Def.GlowLightOffset);
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
	UDCUnits* SlotOwner = UDCUnits::Get(this);
	if (SlotOwner && !SlotOwner->TryEngage(*Target, *this, false, Now))
	{
		NextAttackTime = Now + 0.25f;
		return;
	}
	NextAttackTime = Now + Def->AttackInterval;
	++AttackSeq; // R20: every machine plays the attack clip
	const float Hurt = CrewDamage(GetWorld(), Def->Damage);
	Target->HostApplyHurt(SlotOwner ? SlotOwner->SpendCrewDamage(*Target, Hurt, Now) : Hurt, GetActorLocation());
}

void ADCEnemy::Tick(float DeltaSeconds)
{
	DC_FRAME_SCOPE("Enemies");
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
	if (IsAlive() || Anim)
	{
		TickGait(DeltaSeconds); // R20: an animated unit keeps posing through its death clip
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
	static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("dcenemytrace"));
	if (bTrace && GetWorld()->GetTimeSeconds() >= NextTraceTime)
	{
		NextTraceTime = GetWorld()->GetTimeSeconds() + 5.f;
		UE_LOG(LogTemp, Display, TEXT("DCTRACE %s %s at %s moved=%.1f dt=%.3f rushing=%d"), *GetName(), *Def->Id.ToString(), *GetActorLocation().ToCompactString(),
			FVector::Dist(Before, GetActorLocation()), DeltaSeconds, bRushing ? 1 : 0);
	}
}

float ADCEnemy::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !IsAlive() || !CanBeDamaged())
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
			// R3: shooting the friendly army pays nothing and HQ notices (advisor a-7).
			const bool bAlly = !GetFaction().IsNone() && !DCFactionRules::FightsCrew(UDCTuning::Get().Factions, GetFaction());
			PS->AddPoints(bAlly ? 0 : (Def ? Def->KillPoints : UDCTuning::Get().Economy.KillPoints));
			PS->Kills += bAlly ? 0 : 1;
			ADCCharacter* Shooter = bAlly ? PS->GetPawn<ADCCharacter>() : nullptr;
			if (Shooter)
			{
				Shooter->HostToast(TEXT("FRIENDLY FIRE. HQ WILL BILL YOU FOR THE UNIFORM."));
			}
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
	SetLifeSpan(Def && Def->Behaviour == Walker ? 2.5f : 0.3f + (Anim ? Anim->DeathSeconds() : 0.f)); // R20: the death clip plays first
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
	GlowLight->SetVisibility(false);
	DCAudio::PlayAt(this, TEXT("Kill"), GetActorLocation(), 0.4f, 0.7f);
	// R20: an animated unit falls first (its death clip), then bursts; EndPlay bursts if the actor goes before.
	const float Delay = Anim ? Anim->DeathSeconds() : 0.f;
	if (Delay > 0.f)
	{
		GetWorldTimerManager().SetTimer(BurstTimer, this, &ADCEnemy::BurstBody, Delay, false);
		return;
	}
	BurstBody();
}

void ADCEnemy::BurstBody()
{
	if (bBodyBurst)
	{
		return;
	}
	bBodyBurst = true;
	if (Anim && ModelComp)
	{
		ModelComp->SetVisibility(false);
	}
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr;
	const FDCEnemyDef* Def = GetDef();
	if (!Pool || !Def)
	{
		return;
	}
	// R3: a unit bursts into voxel crumbs of its body and its light cue (instanced, cheap), plus a couple of tumbling
	// debris blocks.
	if (ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		City->BurstCrumbs(GetActorLocation(), Def->Color * 3.f, Def->EyeColor, FMath::RoundToInt32(24 * FMath::Max(Def->Scale, 0.5f)), 5.f * FMath::Max(Def->Scale, 1.f) * 2.f);
	}
	for (int32 i = 0; i < 2; ++i)
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
