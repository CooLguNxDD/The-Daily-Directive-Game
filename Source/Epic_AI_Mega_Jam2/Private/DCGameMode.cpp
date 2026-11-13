#include "DCGameMode.h"

#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCHUD.h"
#include "DCOrbitalStrike.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ADCGameMode::ADCGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = ADCCharacter::StaticClass();
	PlayerControllerClass = ADCPlayerController::StaticClass();
	PlayerStateClass = ADCPlayerState::StaticClass();
	GameStateClass = ADCGameState::StaticClass();
	HUDClass = ADCHUD::StaticClass();
	bUseSeamlessTravel = false;
}

void ADCGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	RequestedSeed = UGameplayStatics::GetIntOption(Options, TEXT("seed"), 0);
}

void ADCGameMode::BeginPlay()
{
	Super::BeginPlay();
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->RunSeed = RequestedSeed != 0 ? RequestedSeed : FMath::RandRange(1000, 999999);
	StartSector(0);
}

FTransform ADCGameMode::SpawnTransformFor(int32 PlayerIndex) const
{
	// Spawn on the outer street, facing the city centre.
	const FVector Edge(-CityPlan.HalfExtent - 600.f, (PlayerIndex - 1.5f) * 250.f, 120.f);
	return FTransform(FRotator(0, 0, 0), Edge);
}

void ADCGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}
	const int32 Index = GameState ? GameState->PlayerArray.IndexOfByKey(NewPlayer->PlayerState) : 0;
	RestartPlayerAtTransform(NewPlayer, SpawnTransformFor(FMath::Max(Index, 0)));
}

void ADCGameMode::ClearSector()
{
	for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		B->Destroy();
	}
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		E->Destroy();
	}
	for (ADCOrbitalStrike* S : TActorRange<ADCOrbitalStrike>(GetWorld()))
	{
		S->Destroy();
	}
}

void ADCGameMode::StartSector(int32 Index)
{
	ClearSector();
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	const int32 Seed = DCCityRules::SectorSeed(GS->RunSeed, Index);
	SectorRng.Initialize(Seed);
	CityPlan = DCCityRules::PlanCity(Seed, T.City, T.Look.BuildingColors.Num());
	for (const FDCLotPlan& Lot : CityPlan.Buildings)
	{
		ADCBuilding* B = GetWorld()->SpawnActorDeferred<ADCBuilding>(ADCBuilding::StaticClass(), FTransform(Lot.Location));
		B->InitSpec(Lot.Spec);
		B->FinishSpawning(FTransform(Lot.Location));
	}

	GS->SectorIndex = Index;
	GS->Phase = EDCRunPhase::Sector;
	SectorStartTime = GS->Now();
	const FDCSectorDef* Sector = T.Sectors.IsValidIndex(Index) ? &T.Sectors[Index] : nullptr;
	GS->SectorEndTime = SectorStartTime + (Sector ? Sector->DurationSeconds : 240.f);
	const int32 Waves = Sector ? Sector->Waves.Num() : 0;
	WaveSpawned.Init(0, Waves);
	WaveNextTime.Init(0.f, Waves);
	for (int32 W = 0; W < Waves; ++W)
	{
		WaveNextTime[W] = SectorStartTime + Sector->Waves[W].StartSeconds;
	}

	int32 PlayerIndex = 0;
	for (APlayerState* PS : GS->PlayerArray)
	{
		ADCCharacter* C = PS ? PS->GetPawn<ADCCharacter>() : nullptr;
		if (C)
		{
			C->TeleportTo(SpawnTransformFor(PlayerIndex).GetLocation(), FRotator::ZeroRotator);
			C->HostRevive();
			C->HostRefill();
		}
		++PlayerIndex;
	}
}

void ADCGameMode::TickWaves(float Now)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	if (!T.Sectors.IsValidIndex(GS->SectorIndex) || CityPlan.StreetPoints.Num() == 0)
	{
		return;
	}
	const FDCSectorDef& Sector = T.Sectors[GS->SectorIndex];
	int32 Alive = 0;
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		Alive += E->IsAlive() ? 1 : 0;
	}
	for (int32 W = 0; W < Sector.Waves.Num(); ++W)
	{
		const FDCWaveDef& Wave = Sector.Waves[W];
		if (WaveSpawned[W] >= Wave.Count || Now < WaveNextTime[W] || Alive >= Sector.MaxAlive)
		{
			continue;
		}
		const FDCEnemyDef* Def = T.FindEnemy(Wave.EnemyId);
		if (!Def)
		{
			WaveSpawned[W] = Wave.Count;
			continue;
		}
		const FVector Street = CityPlan.StreetPoints[SectorRng.RandRange(0, CityPlan.StreetPoints.Num() - 1)];
		const FVector Where = Street + FVector(SectorRng.FRandRange(-300, 300), SectorRng.FRandRange(-300, 300), Def->HoverHeight);
		const FTransform Xf(FRotator(0, SectorRng.FRandRange(0, 360), 0), Where);
		ADCEnemy* E = GetWorld()->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		E->InitKind(Def->Id);
		E->FinishSpawning(Xf);
		++WaveSpawned[W];
		++Alive;
		WaveNextTime[W] = Now + Wave.Interval;
	}
}

void ADCGameMode::TickSector(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (GS->Phase != EDCRunPhase::Sector || Now < GS->SectorEndTime)
	{
		return;
	}
	const int32 Next = GS->SectorIndex + 1;
	if (UDCTuning::Get().Sectors.IsValidIndex(Next))
	{
		StartSector(Next);
		return;
	}
	ClearSector();
	GS->Phase = EDCRunPhase::FrontPage;
}

void ADCGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HitstopEndReal > 0.0 && GetWorld()->GetRealTimeSeconds() >= HitstopEndReal)
	{
		HitstopEndReal = 0.0;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
	const ADCGameState* GS = GetGameState<ADCGameState>();
	if (!GS)
	{
		return;
	}
	const float Now = GS->Now();
	if (GS->Phase == EDCRunPhase::Sector)
	{
		TickWaves(Now);
		TickSector(Now);
	}
}

void ADCGameMode::RequestHitstop()
{
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	UGameplayStatics::SetGlobalTimeDilation(this, Look.HitstopDilation);
	HitstopEndReal = GetWorld()->GetRealTimeSeconds() + Look.HitstopSeconds;
}

bool ADCGameMode::IsCombatLive() const
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	return GS && GS->Phase == EDCRunPhase::Sector;
}

void ADCGameMode::CallStrike(const FDCStratagemDef& Def, const FVector& Target, AController* Caller)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	FRandomStream Rng(GetTypeHash(Target) ^ GS->RunSeed);
	for (int32 i = 0; i < FMath::Max(Def.Blasts, 1); ++i)
	{
		const FVector2D Scatter = i == 0 ? FVector2D::ZeroVector : FVector2D(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)).GetSafeNormal() * Rng.FRandRange(0.3f, 1.f) * Def.BlastScatterCm;
		const FTransform Xf(FVector(Target.X + Scatter.X, Target.Y + Scatter.Y, Target.Z));
		ADCOrbitalStrike* Strike = GetWorld()->SpawnActorDeferred<ADCOrbitalStrike>(ADCOrbitalStrike::StaticClass(), Xf);
		Strike->Init(Def.Id, GS->Now() + Def.TelegraphSeconds + i * Def.BlastSpacing, Caller);
		Strike->FinishSpawning(Xf);
	}
}

void ADCGameMode::OnPhotoGraded(ADCCharacter* Photographer, const FDCPhotoRecord& Record, const FDCPhotoInputs& Inputs)
{
	ADCPlayerState* PS = Photographer ? Photographer->GetPlayerState<ADCPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	FDCPhotoRecord Booked = Record;
	Booked.Sector = GetGameState<ADCGameState>()->SectorIndex;
	PS->AddPhoto(Booked);
}

void ADCGameMode::OnPlayerDowned(ADCCharacter* Player)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	for (APlayerState* PS : GS->PlayerArray)
	{
		const ADCCharacter* C = PS ? PS->GetPawn<ADCCharacter>() : nullptr;
		if (C && !C->IsDowned())
		{
			return;
		}
	}
	// Everyone is down: the run fails, but the reel still plays.
	ClearSector();
	GS->Phase = EDCRunPhase::FrontPage;
}
