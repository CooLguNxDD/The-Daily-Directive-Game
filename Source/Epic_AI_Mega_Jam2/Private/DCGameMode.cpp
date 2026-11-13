#include "DCGameMode.h"

#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCDemoPilot.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCHUD.h"
#include "DCLanBeacon.h"
#include "DCOrbitalStrike.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCVoxelCity.h"
#include "DCSupplyPod.h"
#include "DCTuning.h"
#include "DCUpgrades.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Rules/DCRunRules.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 MaxDominoGenerations = 2;
	constexpr float FreezeDilation = 0.01f;
}

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
	FParse::Value(FCommandLine::Get(), TEXT("dcseed="), RequestedSeed);
	// Solo from the menu (?solo) and the scripted demo start at once; a listen host waits in the lobby.
	bAutoStart = UGameplayStatics::HasOption(Options, TEXT("solo")) || ADCDemoPilot::IsRequested() || FParse::Param(FCommandLine::Get(), TEXT("dcsolo"))
		|| FParse::Param(FCommandLine::Get(), TEXT("dcsmoke"));
}

const TMap<EDCRunPhase, ADCGameMode::FPhaseHandlers>& ADCGameMode::PhaseTable()
{
	static const TMap<EDCRunPhase, FPhaseHandlers> Table = {
		{EDCRunPhase::Lobby, {nullptr, nullptr}},
		{EDCRunPhase::Sector, {&ADCGameMode::EnterSector, &ADCGameMode::TickSectorPhase}},
		{EDCRunPhase::Draft, {&ADCGameMode::EnterDraft, &ADCGameMode::TickDraft}},
		{EDCRunPhase::Reel, {&ADCGameMode::EnterReel, &ADCGameMode::TickReel}},
		{EDCRunPhase::FinalCut, {&ADCGameMode::EnterFinalCut, &ADCGameMode::TickFinalCut}},
		{EDCRunPhase::Vote, {&ADCGameMode::EnterVote, &ADCGameMode::TickVote}},
		{EDCRunPhase::FrontPage, {&ADCGameMode::EnterFrontPage, nullptr}},
	};
	return Table;
}

void ADCGameMode::EnterPhase(EDCRunPhase Phase)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->Phase = Phase;
	GS->ForceNetUpdate();
	const FPhaseHandlers* H = PhaseTable().Find(Phase);
	if (H && H->Enter)
	{
		(this->*H->Enter)(GS->Now());
	}
}

void ADCGameMode::BeginPlay()
{
	Super::BeginPlay();
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->RunSeed = RequestedSeed != 0 ? RequestedSeed : FMath::RandRange(1000, 999999);
	GS->VoxelOps.Reset();
	BuildCity(0); // the lobby's backdrop is the first sector
	if (GetNetMode() == NM_ListenServer)
	{
		if (UDCLanBeacon* Beacon = GetGameInstance()->GetSubsystem<UDCLanBeacon>())
		{
			Beacon->StartAnnouncing(UDCTuning::Get().Run.Port, TEXT("Front Page Press Corps"));
		}
	}
	// Scripted co-op runs (-dcwaitplayers=N) hold the lobby until N players joined.
	FParse::Value(FCommandLine::Get(), TEXT("dcwaitplayers="), AutoStartPlayers);
	if (bAutoStart && AutoStartPlayers <= 1)
	{
		StartRun();
	}
	if (ADCDemoPilot::IsRequested())
	{
		GetWorld()->SpawnActor<ADCDemoPilot>();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("dcsmoke")))
	{
		StartSmoke();
	}
}

void ADCGameMode::StartSmoke()
{
	// Packaged-build smoke (-dcsmoke, run by the gate's full tier on the archived exe): the run starts, HQ drops a
	// laser on a tower, the tower collapses, then the game quits with one DCSMOKE line the gate checks.
	FTimerHandle Strike, Verdict;
	GetWorldTimerManager().SetTimer(Strike, FTimerDelegate::CreateWeakLambda(this, [this]
	{
		const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(TEXT("OrbitalLaser"));
		for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
		{
			if (Def && !B->IsCollapsed())
			{
				CallStrike(*Def, B->GetActorLocation(), nullptr);
				break;
			}
		}
	}), 2.f, false);
	GetWorldTimerManager().SetTimer(Verdict, FTimerDelegate::CreateWeakLambda(this, [this]
	{
		const ADCGameState* GS = GetGameState<ADCGameState>();
		int32 Towers = 0;
		for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
		{
			++Towers;
		}
		const bool bPass = GS && GS->Phase == EDCRunPhase::Sector && Towers > 0 && GS->Blasts.Num() > 0 && GS->Collapses.Num() > 0 && Players().Num() > 0;
		UE_LOG(LogTemp, Display, TEXT("DCSMOKE %s phase=%d towers=%d blasts=%d collapses=%d players=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"),
			GS ? static_cast<int32>(GS->Phase) : -1, Towers, GS ? GS->Blasts.Num() : 0, GS ? GS->Collapses.Num() : 0, Players().Num());
		FGenericPlatformMisc::RequestExit(false);
	}), 9.f, false);
}

TArray<ADCPlayerState*> ADCGameMode::Players() const
{
	TArray<ADCPlayerState*> Out;
	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (ADCPlayerState* DC = Cast<ADCPlayerState>(PS))
		{
			Out.Add(DC);
		}
	}
	return Out;
}

void ADCGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	// Co-op is 1-4 (R12): a full crew refuses the join with a reason the client logs.
	if (ErrorMessage.IsEmpty() && GameState && GameState->PlayerArray.Num() >= UDCTuning::Get().Run.MaxPlayers)
	{
		ErrorMessage = FString::Printf(TEXT("The crew is full (%d photographers)."), UDCTuning::Get().Run.MaxPlayers);
	}
}

void ADCGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	if (UDCLanBeacon* Beacon = GetGameInstance()->GetSubsystem<UDCLanBeacon>())
	{
		Beacon->SetPlayerCount(GameState->PlayerArray.Num()); // the LAN list shows who is still in the lobby
	}
}

void ADCGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	const TArray<FString>& Signs = UDCTuning::Get().Run.CallSigns;
	if (UDCLanBeacon* Beacon = GetGameInstance()->GetSubsystem<UDCLanBeacon>())
	{
		Beacon->SetPlayerCount(GameState->PlayerArray.Num());
	}
	// First call sign no other player holds (a rejoin after a leave must not duplicate one) (R8).
	ADCPlayerState* Mine = NewPlayer->GetPlayerState<ADCPlayerState>();
	if (!Mine)
	{
		return;
	}
	TSet<FString> Taken;
	for (const APlayerState* Other : GameState->PlayerArray)
	{
		const ADCPlayerState* DC = Cast<ADCPlayerState>(Other);
		if (DC && DC != Mine && !DC->GetCallSign().IsEmpty())
		{
			Taken.Add(DC->GetCallSign());
		}
	}
	const FString* Free = Signs.FindByPredicate([&Taken](const FString& S) { return !Taken.Contains(S); });
	Mine->SetCallSign(Free ? *Free : FString::Printf(TEXT("CORRESPONDENT %d"), GameState->PlayerArray.Num()));
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

float ADCGameMode::RogueInterval() const
{
	float Interval = 0.f;
	for (const FDCUpgradeDef* Mutator : GetGameState<ADCGameState>()->ActiveMutators())
	{
		Interval = Mutator->RogueStrikeSeconds > 0.f ? Mutator->RogueStrikeSeconds : Interval;
	}
	return Interval;
}

// ---------------------------------------------------------------- run start / restart

void ADCGameMode::StartRun()
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (GS->Phase != EDCRunPhase::Lobby)
	{
		return;
	}
	if (UDCLanBeacon* Beacon = GetGameInstance()->GetSubsystem<UDCLanBeacon>())
	{
		Beacon->Stop(); // the lobby closed
	}
	GS->SectorIndex = 0;
	// Evidence runs can start in a later sector (-dcstartsector=N) to reach its enemies sooner.
	FParse::Value(FCommandLine::Get(), TEXT("dcstartsector="), GS->SectorIndex);
	GS->SectorIndex = FMath::Clamp(GS->SectorIndex, 0, FMath::Max(UDCTuning::Get().Sectors.Num() - 1, 0));
	EnterPhase(EDCRunPhase::Sector);
}

void ADCGameMode::RestartRun()
{
	const FString Map = GetWorld()->GetMapName().Replace(*GetWorld()->StreamingLevelsPrefix, TEXT(""));
	if (GetNetMode() == NM_ListenServer)
	{
		GetWorld()->ServerTravel(Map + TEXT("?listen"), true);
		return;
	}
	UGameplayStatics::OpenLevel(this, FName(*Map), true, TEXT("solo"));
}

// ---------------------------------------------------------------- city

void ADCGameMode::ClearCombat()
{
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		E->Destroy();
	}
	for (ADCOrbitalStrike* S : TActorRange<ADCOrbitalStrike>(GetWorld()))
	{
		S->Destroy();
	}
	for (ADCSupplyPod* P : TActorRange<ADCSupplyPod>(GetWorld()))
	{
		P->Destroy();
	}
	GetGameState<ADCGameState>()->Lures.Reset();
}

void ADCGameMode::ClearCity()
{
	for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		B->Destroy();
	}
	DominoGeneration.Reset();
	PendingCollapses.Reset();
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (GS && GS->CitySector != INDEX_NONE)
	{
		GS->CitySector = INDEX_NONE;
		++GS->CityBuildId;
		if (ADCVoxelCity* City = GS->GetVoxelCity())
		{
			City->Clear();
		}
	}
}

void ADCGameMode::BuildCity(int32 SectorIndex)
{
	ClearCity();
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	const int32 Seed = DCCityRules::SectorSeed(GS->RunSeed, SectorIndex);
	SectorRng.Initialize(Seed);
	CityPlan = ADCVoxelCity::PlanSector(GS->RunSeed, SectorIndex); // recipes applied when the voxel city is on (R2)
	{
		// Every machine's voxel city follows these two fields; the host's builds now when it exists already.
		ADCGameState* MutableGS = GetGameState<ADCGameState>();
		MutableGS->CitySector = SectorIndex;
		++MutableGS->CityBuildId;
		if (ADCVoxelCity* City = GS->GetVoxelCity())
		{
			City->Build(GS->RunSeed, SectorIndex, MutableGS->CityBuildId);
		}
	}
	for (int32 Lot = 0; Lot < CityPlan.Buildings.Num(); ++Lot)
	{
		const FDCLotPlan& Plan = CityPlan.Buildings[Lot];
		ADCBuilding* B = GetWorld()->SpawnActorDeferred<ADCBuilding>(ADCBuilding::StaticClass(), FTransform(Plan.Location));
		B->InitSpec(Plan.Spec, Lot);
		B->FinishSpawning(FTransform(Plan.Location));
	}
}

// ---------------------------------------------------------------- sector

void ADCGameMode::EnterSector(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	SetDilation(1.f);
	HitstopEndReal = 0.0;
	ClearCombat();
	BuildCity(GS->SectorIndex);

	SectorStartTime = Now;
	LastPackTime = -1e6f;
	LastDownTime = -1e6f;
	const FDCSectorDef* Sector = T.Sectors.IsValidIndex(GS->SectorIndex) ? &T.Sectors[GS->SectorIndex] : nullptr;
	float Duration = Sector ? Sector->DurationSeconds : 240.f;
	FParse::Value(FCommandLine::Get(), TEXT("dcsectorseconds="), Duration); // short sectors for scripted test runs
	GS->SectorEndTime = SectorStartTime + Duration;
	GS->SectorStartTime = SectorStartTime;
	const int32 Waves = Sector ? Sector->Waves.Num() : 0;
	WaveSpawned.Init(0, Waves);
	WaveNextTime.Init(0.f, Waves);
	for (int32 W = 0; W < Waves; ++W)
	{
		WaveNextTime[W] = SectorStartTime + Sector->Waves[W].StartSeconds;
	}
	GS->Scoop = FDCScoopState();
	GS->SectorPhotoPoints = 0;
	NextScoopTime = Now + T.Run.ScoopFirstSeconds;
	NextRogueTime = Now + RogueInterval();

	int32 PlayerIndex = 0;
	for (ADCPlayerState* PS : Players())
	{
		if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
		{
			C->TeleportTo(SpawnTransformFor(PlayerIndex).GetLocation(), FRotator::ZeroRotator);
			C->HostRevive();
			C->HostRefill();
		}
		++PlayerIndex;
	}
}

void ADCGameMode::TickSectorPhase(float Now)
{
	// Evidence runs (-dcdownhost=S): the host player is knocked down S seconds into the first sector, so a
	// teammate's revive can be recorded. Never active otherwise.
	// -dcdownall=S knocks every player down instead (a co-op wipe: the run fails and the reel still plays).
	static const TPair<const TCHAR*, int32> DownSwitches[] = {{TEXT("dcdownhost="), 1}, {TEXT("dcdownall="), MAX_int32}};
	for (const TPair<const TCHAR*, int32>& Switch : DownSwitches)
	{
		float DownAt = 0.f;
		if (bScriptedDownDone || !FParse::Value(FCommandLine::Get(), Switch.Key, DownAt) || Now < SectorStartTime + DownAt)
		{
			continue;
		}
		bScriptedDownDone = true;
		const TArray<ADCPlayerState*> All = Players();
		for (int32 i = 0; i < All.Num() && i < Switch.Value; ++i)
		{
			if (ADCCharacter* C = All[i]->GetPawn<ADCCharacter>())
			{
				C->HostApplyHurt(10000.f, C->GetActorLocation() + FVector(500, 0, 0));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("DCDOWN scripted %s players knocked down"), Switch.Value == 1 ? TEXT("host") : TEXT("all"));
	}
	CheckWipe();
	if (GetGameState<ADCGameState>()->Phase != EDCRunPhase::Sector)
	{
		return;
	}
	TickWaves(Now);
	TickPressure(Now);
	TickPendingCollapses(Now);
	TickScoop(Now);
	TickRogueStrikes(Now);
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (Now < GS->SectorEndTime)
	{
		return;
	}
	ClearCombat();
	const UDCTuning& T = UDCTuning::Get();
	const bool bMore = T.Sectors.IsValidIndex(GS->SectorIndex + 1);
	const int32 Quota = T.Sectors.IsValidIndex(GS->SectorIndex) ? T.Sectors[GS->SectorIndex].QuotaPoints : 0;
	const bool bQuotaMet = GS->SectorPhotoPoints >= Quota;
	GS->QuotaResults.Add(bQuotaMet);
	for (ADCPlayerState* PS : Players())
	{
		if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
		{
			C->HostToast(bQuotaMet ? FString::Printf(TEXT("QUOTA MET: %d / %d. HQ IS PLEASED."), GS->SectorPhotoPoints, Quota)
				: FString::Printf(TEXT("QUOTA MISSED: %d / %d. HQ WITHHOLDS YOUR REQUISITION."), GS->SectorPhotoPoints, Quota));
		}
	}
	if (!bMore)
	{
		EnterPhase(EDCRunPhase::Reel);
		return;
	}
	if (bQuotaMet)
	{
		EnterPhase(EDCRunPhase::Draft);
		return;
	}
	// Quota missed (R6): no draft, straight into the next sector.
	GS->SectorIndex += 1;
	EnterPhase(EDCRunPhase::Sector);
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
		if (WaveSpawned[W] >= Wave.Count || Now < WaveNextTime[W] || (Alive >= Sector.MaxAlive && !Wave.bIgnoresCap))
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

void ADCGameMode::TickPressure(float Now)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	if (!T.Sectors.IsValidIndex(GS->SectorIndex) || CityPlan.StreetPoints.Num() == 0)
	{
		return;
	}
	const FDCSectorDef& Sector = T.Sectors[GS->SectorIndex];
	const FDCEnemyDef* Def = T.FindEnemy(Sector.Pressure.EnemyId);
	if (!Def)
	{
		return;
	}
	FDCPressureInputs In;
	In.SectorElapsed = Now - SectorStartTime;
	In.SectorDuration = GS->SectorEndTime - SectorStartTime; // the real length (evidence runs shorten it)
	In.PhotoPoints = GS->SectorPhotoPoints;
	In.QuotaPoints = Sector.QuotaPoints;
	In.SinceDown = Now - LastDownTime;
	In.SinceLastPack = Now - LastPackTime;
	for (const FDCCollapseRecord& C : GS->Collapses)
	{
		In.SinceCollapse = C.Time <= Now ? FMath::Min(In.SinceCollapse, Now - C.Time) : In.SinceCollapse;
	}
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		In.Alive += E->IsAlive() && E->GetKind() == Def->Id ? 1 : 0;
	}
	TArray<ADCCharacter*> Standing;
	for (ADCPlayerState* PS : Players())
	{
		ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		if (C && !C->IsDowned())
		{
			Standing.Add(C);
		}
	}
	In.PlayersStanding = Standing.Num();
	const FDCPressureDecision D = DCPressureRules::Decide(In, Sector.Pressure);
	if (D.PackSize <= 0)
	{
		return;
	}
	// A pack lands on a street point in a ring around one standing player, then hunts.
	const FVector Near = Standing[SectorRng.RandRange(0, Standing.Num() - 1)]->GetActorLocation();
	TArray<FVector> Ring;
	for (const FVector& P : CityPlan.StreetPoints)
	{
		const float Dist = FVector::Dist2D(P, Near);
		if (Dist >= Sector.Pressure.RingMinCm && Dist <= Sector.Pressure.RingMaxCm)
		{
			Ring.Add(P);
		}
	}
	const TArray<FVector>& Pool = Ring.Num() > 0 ? Ring : CityPlan.StreetPoints;
	const FVector Centre = Pool[SectorRng.RandRange(0, Pool.Num() - 1)];
	for (int32 i = 0; i < D.PackSize; ++i)
	{
		const FVector Where = Centre + FVector(SectorRng.FRandRange(-400, 400), SectorRng.FRandRange(-400, 400), Def->HoverHeight);
		const FTransform Xf(FRotator(0, SectorRng.FRandRange(0, 360), 0), Where);
		// Never inside a tower: a spot that would collide is nudged clear or skipped.
		ADCEnemy* E = GetWorld()->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (E)
		{
			E->InitKind(Def->Id);
			E->FinishSpawning(Xf);
		}
	}
	LastPackTime = Now;
	UE_LOG(LogTemp, Display, TEXT("DCPACK sector=%d t=%.0f pack=%d alive=%d target=%d lag=%.2f photo=%d/%d"), GS->SectorIndex + 1, In.SectorElapsed, D.PackSize, In.Alive,
		D.TargetAlive, D.Lag, In.PhotoPoints, In.QuotaPoints);
}

void ADCGameMode::TickScoop(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	FDCScoopState& Scoop = GS->Scoop;
	const bool bLive = Scoop.Index >= 0 && !Scoop.bDone && Now < Scoop.EndTime;
	if (bLive)
	{
		return;
	}
	if (Scoop.Index >= 0 && !Scoop.bDone && Now >= Scoop.EndTime)
	{
		Scoop.Index = -1; // expired unclaimed
		NextScoopTime = Now + T.Run.ScoopGapSeconds;
	}
	if (Now < NextScoopTime || Now > GS->SectorEndTime - 20.f || T.Scoops.Num() == 0)
	{
		return;
	}
	Scoop.Index = DCRunRules::PickScoop(SectorRng, T.Scoops.Num(), LastScoop);
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("dcscoop="), Forced)) // evidence runs: force one template
	{
		const int32 ForcedIndex = T.Scoops.IndexOfByPredicate([&Forced](const FDCScoopDef& D) { return D.Id == FName(*Forced); });
		Scoop.Index = ForcedIndex != INDEX_NONE ? ForcedIndex : Scoop.Index;
	}
	LastScoop = Scoop.Index;
	Scoop.bDone = false;
	Scoop.CompletedBy.Reset();
	Scoop.EndTime = Now + T.Scoops[Scoop.Index].Seconds;
	NextScoopTime = Scoop.EndTime + T.Run.ScoopGapSeconds;
	for (ADCPlayerState* PS : Players())
	{
		if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
		{
			C->HostCue(TEXT("Scoop"));
		}
	}
}

void ADCGameMode::TickRogueStrikes(float Now)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const float Interval = RogueInterval();
	if (Interval <= 0.f || Now < NextRogueTime)
	{
		return;
	}
	NextRogueTime = Now + Interval;
	UE_LOG(LogTemp, Display, TEXT("DCROGUE free strike due (every %.0f s)"), Interval);
	TArray<ADCBuilding*> Standing;
	for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		if (!B->IsCollapsed())
		{
			Standing.Add(B);
		}
	}
	const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(TEXT("OrbitalLaser"));
	if (Standing.Num() > 0 && Def)
	{
		const FVector Target = Standing[SectorRng.RandRange(0, Standing.Num() - 1)]->GetActorLocation();
		CallStrike(*Def, Target, nullptr);
		// Every player hears where the free strike lands, so it reads as a photo chance, not noise off camera.
		for (ADCPlayerState* PS : Players())
		{
			if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
			{
				const float Metres = FVector::Dist2D(C->GetActorLocation(), Target) / 100.f;
				C->HostToast(FString::Printf(TEXT("ROGUE ORBITAL STRIKE INBOUND  %.0f M"), Metres));
			}
		}
	}
}

// ---------------------------------------------------------------- draft (R10)

void ADCGameMode::EnterDraft(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	SetDilation(1.f);
	HitstopEndReal = 0.0;
	const UDCTuning& T = UDCTuning::Get();
	GS->PhaseEndTime = Now + T.Run.DraftSeconds;
	int32 Index = 0;
	for (ADCPlayerState* PS : Players())
	{
		TArray<FName> Taken = PS->Upgrades;
		Taken.Append(GS->Mutators);
		PS->DraftOffer = DCRunRules::DraftOffer(DCCityRules::SectorSeed(GS->RunSeed, GS->SectorIndex) ^ (++Index * 7919), T.Upgrades, Taken);
		PS->bDraftDone = PS->DraftOffer.Num() == 0;
	}
}

void ADCGameMode::PickUpgrade(ADCPlayerState* PS, FName UpgradeId)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const FDCUpgradeDef* Def = UDCTuning::Get().FindUpgrade(UpgradeId);
	if (!PS || !Def || GS->Phase != EDCRunPhase::Draft || PS->bDraftDone || !PS->DraftOffer.Contains(UpgradeId))
	{
		return;
	}
	PS->bDraftDone = true;
	PS->Upgrades.AddUnique(UpgradeId);
	DCUpgrades::Apply(*GS, *PS, *Def);
}

void ADCGameMode::TickDraft(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	TArray<ADCPlayerState*> All = Players();
	const bool bAllDone = !All.ContainsByPredicate([](const ADCPlayerState* PS) { return !PS->bDraftDone; });
	if (!bAllDone && Now < GS->PhaseEndTime)
	{
		return;
	}
	for (ADCPlayerState* PS : All)
	{
		if (!PS->bDraftDone && PS->DraftOffer.Num() > 0)
		{
			PickUpgrade(PS, PS->DraftOffer[0]); // the clock ran out: HQ picks for you
		}
	}
	GS->SectorIndex += 1;
	EnterPhase(EDCRunPhase::Sector);
}

// ---------------------------------------------------------------- photos, collapses, downs

void ADCGameMode::OnPhotoGraded(ADCCharacter* Photographer, const FDCPhotoRecord& Record, const FDCPhotoInputs& Inputs, TArray<FDCPoseSample>&& Poses)
{
	ADCPlayerState* PS = Photographer ? Photographer->GetPlayerState<ADCPlayerState>() : nullptr;
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!PS)
	{
		return;
	}
	FDCPhotoRecord Booked = Record;
	Booked.Sector = GS->SectorIndex;
	// Co-star (R20): both names on the byline, and the teammate earns a share of the photo's points.
	if (Record.Grade.CoStarMultiplier > 1.f && Inputs.CoStarPlayerId >= 0)
	{
		Booked.CoStar = Inputs.CoStarName;
		for (APlayerState* Other : GS->PlayerArray)
		{
			ADCPlayerState* Mate = Cast<ADCPlayerState>(Other);
			if (Mate && Mate->GetPlayerId() == Inputs.CoStarPlayerId)
			{
				const int32 Share = FMath::RoundToInt32(Record.Grade.Score * UDCTuning::Get().Photo.CoStarShare);
				Mate->AddPoints(Share);
				UE_LOG(LogTemp, Display, TEXT("DCCOSTAR photo=%d by=%s costar=%s x%.2f share=%d"), Record.PhotoId, *PS->GetPlayerName(), *Inputs.CoStarName, Record.Grade.CoStarMultiplier, Share);
			}
		}
	}
	PS->AddPhoto(Booked);
	PhotoPoses.Add(Record.PhotoId, MoveTemp(Poses));
	GS->SectorPhotoPoints += Record.Grade.Score;

	// HQ Scoop (R8): the first photo that meets it drops a supply pod on the photographer at once.
	const UDCTuning& T = UDCTuning::Get();
	FDCScoopState& Scoop = GS->Scoop;
	if (Scoop.Index < 0 || Scoop.bDone || GS->Now() > Scoop.EndTime || !T.Scoops.IsValidIndex(Scoop.Index))
	{
		return;
	}
	const FDCScoopDef& Def = T.Scoops[Scoop.Index];
	if (!DCRunRules::ScoopMet(Def, Inputs, Record.Grade))
	{
		return;
	}
	Scoop.bDone = true;
	Scoop.CompletedBy = PS->GetPlayerName();
	UE_LOG(LogTemp, Display, TEXT("DCSCOOP %s filed by %s photo=%d reward=%d"), *Def.Id.ToString(), *Scoop.CompletedBy, Record.PhotoId, Def.RewardPoints);
	PS->AddPoints(Def.RewardPoints);
	ADCSupplyPod::Drop(GetWorld(), Photographer->GetActorLocation() + Photographer->GetActorForwardVector() * 700.f);
	for (ADCPlayerState* Other : Players())
	{
		if (ADCCharacter* C = Other->GetPawn<ADCCharacter>())
		{
			C->HostToast(FString::Printf(TEXT("HQ SCOOP FILED BY %s! +%d  SUPPLY POD INBOUND"), *Scoop.CompletedBy.ToUpper(), Def.RewardPoints));
		}
	}
}

void ADCGameMode::OnBuildingCollapsed(ADCBuilding* Building)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->AddCollapse(GS->SectorIndex, Building->GetLotIndex(), Building->GetEpicenter(), Building->GetCollapseTime());
	const TArray<const FDCUpgradeDef*> Active = GS->ActiveMutators();
	const FDCUpgradeDef* const* Domino = Active.FindByPredicate([](const FDCUpgradeDef* D) { return D->DominoReachCm > 0.f; });
	if (!Domino)
	{
		return;
	}
	// Domino Condemnation: a falling tower drags its neighbours down a beat later, two generations deep.
	const int32 Generation = DominoGeneration.FindRef(Building);
	if (Generation >= MaxDominoGenerations)
	{
		return;
	}
	const float Reach = (*Domino)->DominoReachCm;
	const float Delay = FMath::Max((*Domino)->DominoDelaySeconds, 0.1f);
	for (ADCBuilding* Other : TActorRange<ADCBuilding>(GetWorld()))
	{
		if (Other == Building || Other->IsCollapsed() || FVector::Dist2D(Other->GetActorLocation(), Building->GetActorLocation()) > Reach)
		{
			continue;
		}
		DominoGeneration.Add(Other, Generation + 1);
		ScheduleCollapse(Other, Building->GetActorLocation(), TEXT("Domino"), Delay);
	}
}

void ADCGameMode::ScheduleCollapse(ADCBuilding* Building, const FVector& Epicenter, FName Kind, float Delay, float AtTime)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!Building || Building->IsCollapsed())
	{
		return;
	}
	if (Delay <= 0.f)
	{
		// Immediate (a strike's own record is the event): beats any telegraphed collapse still pending on it, and
		// that collapse's future peak is withdrawn so photos are not graded against a peak that never comes.
		if (PendingCollapses.RemoveAll([Building](const FPendingCollapse& P) { return P.Building == Building; }) > 0)
		{
			const float Now = GS->Now();
			const int32 Lot = Building->GetLotIndex();
			const int32 Sector = GS->SectorIndex;
			GS->Blasts.RemoveAll([Now, Lot, Sector](const FDCBlastRecord& R) { return R.LotIndex == Lot && R.Sector == Sector && R.Time > Now; });
		}
		Building->Collapse(Epicenter, AtTime);
		return;
	}
	if (PendingCollapses.ContainsByPredicate([Building](const FPendingCollapse& P) { return P.Building == Building; }))
	{
		return;
	}
	const FDCBuildingSpec& Spec = Building->GetSpec();
	const float Radius = FMath::Max(Spec.SizeX, Spec.SizeY) * Spec.BlockSize;
	const float Peak = GS->Now() + Delay;
	const int32 EventId = GS->AddEvent(Kind, Building->GetActorLocation(), Radius, Peak, Building->GetLotIndex());
	PendingCollapses.Add({Building, Epicenter, Peak});
	UE_LOG(LogTemp, Display, TEXT("DCEVENT %d kind=%s lot=%d telegraph=%.2f peak=%.2f"), EventId, *Kind.ToString(), Building->GetLotIndex(), GS->Now(), Peak);
}

void ADCGameMode::TickPendingCollapses(float Now)
{
	for (int32 i = PendingCollapses.Num() - 1; i >= 0; --i)
	{
		if (Now < PendingCollapses[i].At)
		{
			continue;
		}
		const FPendingCollapse Due = PendingCollapses[i];
		PendingCollapses.RemoveAtSwap(i);
		if (ADCBuilding* B = Due.Building.Get())
		{
			B->Collapse(Due.Epicenter);
		}
	}
}

void ADCGameMode::OnPlayerDowned(ADCCharacter* Player)
{
	LastDownTime = GetGameState<ADCGameState>()->Now(); // pressure relief: no pack right after a down
	// Last Frame (R21): slow motion only for the dying player, so only when they are the only player.
	const FDCPhotoTuning& P = UDCTuning::Get().Photo;
	if (GetNumPlayers() == 1 && P.LastFrameSlowMo > 0.f)
	{
		SetDilation(P.LastFrameSlowMo);
		HitstopEndReal = GetWorld()->GetRealTimeSeconds() + P.LastFrameSlowMoSeconds;
	}
	CheckWipe();
}

void ADCGameMode::CheckWipe()
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (GS->Phase != EDCRunPhase::Sector)
	{
		return;
	}
	for (ADCPlayerState* PS : Players())
	{
		const ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		// Anyone standing, or a solo player still inside the self-revive window, keeps the run alive.
		if (C && (!C->IsDowned() || C->CanSelfRevive()))
		{
			return;
		}
	}
	// Everyone is down: the run fails, but the reel still plays (R12).
	UE_LOG(LogTemp, Display, TEXT("DCWIPE all %d players down in sector %d: run failed, rolling the reel"), Players().Num(), GS->SectorIndex + 1);
	GS->bRunFailed = true;
	ClearCombat();
	EnterPhase(EDCRunPhase::Reel);
}

// ---------------------------------------------------------------- reel (R13)

void ADCGameMode::EnterReel(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const FDCRunTuning& Run = UDCTuning::Get().Run;
	ClearCombat();
	ClearCity();

	// Each player's best shots, by the critic; the reel interleaves them rank by rank.
	TArray<TArray<int32>> Ranked;
	for (ADCPlayerState* PS : Players())
	{
		const TArray<int32> Order = DCRunRules::CriticOrder(PS->Photos);
		PS->FinalCutChoices.Reset();
		for (int32 i = 0; i < Order.Num() && i < Run.FinalCutChoices; ++i)
		{
			PS->FinalCutChoices.Add(PS->Photos[Order[i]].PhotoId);
		}
		Ranked.Add(Order);
		if (ADCPlayerController* PC = Cast<ADCPlayerController>(PS->GetOwningController()))
		{
			PC->ClientUploadPhotos(PS->FinalCutChoices);
		}
	}
	GS->ReelClips.Reset();
	const TArray<ADCPlayerState*> All = Players();
	for (int32 Rank = 0; Rank < Run.ReelClipsPerPlayer; ++Rank)
	{
		for (int32 P = 0; P < All.Num(); ++P)
		{
			if (!Ranked[P].IsValidIndex(Rank))
			{
				continue;
			}
			FDCReelClip& Clip = GS->ReelClips.AddDefaulted_GetRef();
			Clip.Photo = All[P]->Photos[Ranked[P][Rank]];
			Clip.PlayerName = All[P]->GetPlayerName();
			if (const TArray<FDCPoseSample>* Poses = PhotoPoses.Find(Clip.Photo.PhotoId))
			{
				Clip.Poses = *Poses;
			}
		}
	}
	if (GS->ReelClips.Num() == 0)
	{
		EnterPhase(EDCRunPhase::FinalCut);
		return;
	}
	BeginReelClip(0);
}

void ADCGameMode::SetDilation(float Value)
{
	GetWorldSettings()->SetTimeDilation(Value);
	GetWorldSettings()->ForceNetUpdate();
}

void ADCGameMode::BeginReelClip(int32 Index)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->ReelIndex = Index;
	GS->ReelStage = 0;
	GS->ReelClipStart = GS->Now() + 0.3f; // a beat for every machine to re-stage the scene
	ReelStageReal = GetWorld()->GetRealTimeSeconds();
	SetDilation(UDCTuning::Get().Run.ReelSlowMo);
	GS->ForceNetUpdate();
}

void ADCGameMode::TickReel(float Now)
{
	// One handler per reel stage: playing (slow motion) -> frozen at the shutter -> the photo with its stamp.
	using FStageFn = void (ADCGameMode::*)(float);
	static const FStageFn Stages[] = {&ADCGameMode::TickReelPlay, &ADCGameMode::TickReelFreeze, &ADCGameMode::TickReelPhoto};
	const uint8 Stage = GetGameState<ADCGameState>()->ReelStage;
	(this->*Stages[FMath::Min<int32>(Stage, UE_ARRAY_COUNT(Stages) - 1)])(Now);
}

void ADCGameMode::SetReelStage(uint8 Stage, float Dilation)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->ReelStage = Stage;
	ReelStageReal = GetWorld()->GetRealTimeSeconds();
	SetDilation(Dilation);
	GS->ForceNetUpdate();
}

void ADCGameMode::TickReelPlay(float Now)
{
	if (Now >= GetGameState<ADCGameState>()->ReelClipStart + UDCTuning::Get().Run.ClipSeconds)
	{
		SetReelStage(1, FreezeDilation);
	}
}

void ADCGameMode::TickReelFreeze(float Now)
{
	if (GetWorld()->GetRealTimeSeconds() >= ReelStageReal + UDCTuning::Get().Run.ReelFreezeSeconds)
	{
		SetReelStage(2, 1.f);
	}
}

void ADCGameMode::TickReelPhoto(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (GetWorld()->GetRealTimeSeconds() < ReelStageReal + UDCTuning::Get().Run.ReelPhotoSeconds)
	{
		return;
	}
	if (GS->ReelClips.IsValidIndex(GS->ReelIndex + 1))
	{
		BeginReelClip(GS->ReelIndex + 1);
		return;
	}
	GS->ReelIndex = -1;
	EnterPhase(EDCRunPhase::FinalCut);
}

// ---------------------------------------------------------------- final cut (R14) and vote (R15)

void ADCGameMode::EnterFinalCut(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	SetDilation(1.f);
	GS->PhaseEndTime = Now + UDCTuning::Get().Run.FinalCutSeconds;
	for (ADCPlayerState* PS : Players())
	{
		PS->FinalCut.Reset();
		PS->bFinalCutDone = PS->FinalCutChoices.Num() == 0;
	}
}

void ADCGameMode::SubmitFinalCut(ADCPlayerState* PS, const TArray<int32>& PhotoIds)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	if (!PS || GS->Phase != EDCRunPhase::FinalCut || PS->bFinalCutDone)
	{
		return;
	}
	const int32 Want = FMath::Min(UDCTuning::Get().Run.FinalCutPicks, PS->FinalCutChoices.Num());
	TArray<int32> Unique;
	for (int32 Id : PhotoIds)
	{
		if (PS->FinalCutChoices.Contains(Id))
		{
			Unique.AddUnique(Id);
		}
	}
	if (Unique.Num() != Want)
	{
		return;
	}
	PS->FinalCut = Unique;
	PS->bFinalCutDone = true;
}

void ADCGameMode::TickFinalCut(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	TArray<ADCPlayerState*> All = Players();
	const bool bAllDone = !All.ContainsByPredicate([](const ADCPlayerState* PS) { return !PS->bFinalCutDone; });
	if (!bAllDone && Now < GS->PhaseEndTime)
	{
		return;
	}
	const int32 Picks = UDCTuning::Get().Run.FinalCutPicks;
	for (ADCPlayerState* PS : All)
	{
		if (!PS->bFinalCutDone)
		{
			// The clock ran out: the critic's order stands.
			PS->FinalCut.Reset();
			for (int32 i = 0; i < PS->FinalCutChoices.Num() && i < Picks; ++i)
			{
				PS->FinalCut.Add(PS->FinalCutChoices[i]);
			}
			PS->bFinalCutDone = true;
		}
	}
	EnterPhase(All.Num() > 1 ? EDCRunPhase::Vote : EDCRunPhase::FrontPage);
}

void ADCGameMode::EnterVote(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	GS->PhaseEndTime = Now + UDCTuning::Get().Run.VoteSeconds;
	for (ADCPlayerState* PS : Players())
	{
		PS->VotedFor = 0;
	}
}

void ADCGameMode::CastVote(ADCPlayerState* PS, int32 PhotoId)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	if (!PS || GS->Phase != EDCRunPhase::Vote || PS->VotedFor != 0 || PS->FinalCut.Contains(PhotoId))
	{
		return;
	}
	for (ADCPlayerState* Other : Players())
	{
		if (Other != PS && Other->FinalCut.Contains(PhotoId))
		{
			PS->VotedFor = PhotoId;
			return;
		}
	}
}

void ADCGameMode::TickVote(float Now)
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const TArray<ADCPlayerState*> All = Players();
	const bool bAllVoted = !All.ContainsByPredicate([](const ADCPlayerState* PS) { return PS->VotedFor == 0; });
	if (bAllVoted || Now >= GS->PhaseEndTime)
	{
		EnterPhase(EDCRunPhase::FrontPage);
	}
}

void ADCGameMode::EnterFrontPage(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	SetDilation(1.f);
	GS->PagePicks.Reset();
	const TArray<ADCPlayerState*> All = Players();
	int32 Total = 0;
	for (ADCPlayerState* PS : All)
	{
		for (const FDCPhotoRecord& P : PS->Photos)
		{
			Total += P.Grade.Score;
		}
		for (int32 Id : PS->FinalCut)
		{
			const FDCPhotoRecord* Photo = PS->FindPhoto(Id);
			if (!Photo)
			{
				continue;
			}
			FDCPagePick& Pick = GS->PagePicks.AddDefaulted_GetRef();
			Pick.Photo = *Photo;
			Pick.PlayerName = PS->GetPlayerName();
			for (const ADCPlayerState* Voter : All)
			{
				Pick.Votes += Voter->VotedFor == Id ? 1 : 0;
			}
			Pick.PageScore = DCRunRules::PageScore(Photo->Grade.Score, Pick.Votes, T.Run.VoteBonus);
		}
	}
	GS->HeadlineIndex = DCRunRules::HeadlineIndex(GS->PagePicks);
	for (const FDCPagePick& Pick : GS->PagePicks)
	{
		UE_LOG(LogTemp, Display, TEXT("DCPAGE photo=%d by=%s critic=%d votes=%d page=%d%s"), Pick.Photo.PhotoId, *Pick.PlayerName, Pick.Photo.Grade.Score, Pick.Votes, Pick.PageScore,
			GS->PagePicks.IsValidIndex(GS->HeadlineIndex) && &GS->PagePicks[GS->HeadlineIndex] == &Pick ? TEXT(" HEADLINE") : TEXT(""));
	}
	// Full edition (R22): the winner and up to two runner-ups each get a satirical headline from the data rows, never a
	// line this session already printed; a wiped run prints the mock-heroic CORRECTION edition.
	static TSet<FString> PrintedThisSession;
	TArray<int32> Order;
	for (int32 i = 0; i < GS->PagePicks.Num(); ++i)
	{
		Order.Add(i);
	}
	Order.StableSort([GS](int32 A, int32 B) { return GS->PagePicks[A].PageScore > GS->PagePicks[B].PageScore; });
	Order.Remove(GS->HeadlineIndex);
	if (GS->PagePicks.IsValidIndex(GS->HeadlineIndex))
	{
		Order.Insert(GS->HeadlineIndex, 0);
	}
	for (int32 k = 0; k < FMath::Min(3, Order.Num()); ++k)
	{
		FDCPagePick& Pick = GS->PagePicks[Order[k]];
		const FString SectorName = T.Sectors.IsValidIndex(Pick.Photo.Sector) ? T.Sectors[Pick.Photo.Sector].Name.ToString() : TEXT("THE FRONT");
		Pick.Headline = DCRunRules::PickHeadline(GS->RunSeed ^ Pick.Photo.PhotoId, T.HeadlineRows, Pick.Photo.Grade.Categories, Pick.PlayerName, SectorName, Pick.Photo.CoStar, PrintedThisSession);
	}
	if (GS->bRunFailed)
	{
		const FString Correction = DCRunRules::PickHeadline(GS->RunSeed, T.HeadlineRows, {FName(TEXT("CORRECTION"))}, GS->PagePicks.IsValidIndex(GS->HeadlineIndex) ? GS->PagePicks[GS->HeadlineIndex].PlayerName : FString(TEXT("OUR CORRESPONDENTS")),
			TEXT("THE CITY"), FString(), PrintedThisSession);
		GS->Headline = TEXT("CORRECTION: ") + Correction;
	}
	else if (GS->PagePicks.IsValidIndex(GS->HeadlineIndex))
	{
		GS->Headline = GS->PagePicks[GS->HeadlineIndex].Headline;
	}
	else
	{
		GS->Headline = TEXT("CAMERAS SILENT AS CITY STANDS: NO PHOTOS FILED");
	}
	UE_LOG(LogTemp, Display, TEXT("DCPAGE headline=\"%s\"%s"), *GS->Headline, GS->bRunFailed ? TEXT(" CORRECTION EDITION") : TEXT(""));
	const int32 QuotasMet = GS->QuotaResults.FilterByPredicate([](bool b) { return b; }).Num();
	GS->Subhead = FString::Printf(TEXT("%s  |  %d PHOTOGRAPHER%s  |  %s PROPAGANDA POINTS RAISED  |  QUOTAS MET %d/%d  |  RUN SEED %d"),
		GS->bRunFailed ? TEXT("UNIT LOST IN ACTION, REEL RECOVERED") : TEXT("ALL SECTORS LIBERATED"),
		All.Num(), All.Num() == 1 ? TEXT("") : TEXT("S"), *FText::AsNumber(Total).ToString(), QuotasMet, GS->QuotaResults.Num(), GS->RunSeed);
	GS->ForceNetUpdate();
}

// ---------------------------------------------------------------- strikes, hitstop

void ADCGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!GS)
	{
		return;
	}
	if (HitstopEndReal > 0.0 && GetWorld()->GetRealTimeSeconds() >= HitstopEndReal)
	{
		HitstopEndReal = 0.0;
		if (GS->Phase != EDCRunPhase::Reel)
		{
			SetDilation(1.f); // the reel drives dilation itself
		}
	}
	if (ADCDemoPilot::IsRequested() && GetWorld()->GetRealTimeSeconds() >= NextHeartbeatReal)
	{
		// Scripted runs log the run clock so a stall can be told apart from a slow machine.
		NextHeartbeatReal = GetWorld()->GetRealTimeSeconds() + 5.0;
		UE_LOG(LogTemp, Display, TEXT("DCRUN phase=%d sector=%d now=%.1f end=%.1f dilation=%.3f paused=%d hitstop=%.2f"), static_cast<int32>(GS->Phase), GS->SectorIndex,
			GS->Now(), GS->SectorEndTime, GetWorldSettings()->TimeDilation, GetWorld()->IsPaused() ? 1 : 0, HitstopEndReal);
	}
	if (bAutoStart && GS->Phase == EDCRunPhase::Lobby && GS->PlayerArray.Num() >= AutoStartPlayers)
	{
		StartRun();
	}
	const FPhaseHandlers* H = PhaseTable().Find(GS->Phase);
	if (H && H->Tick)
	{
		(this->*H->Tick)(GS->Now());
	}
}

void ADCGameMode::RequestHitstop()
{
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	SetDilation(Look.HitstopDilation);
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
