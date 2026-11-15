#include "DCGameMode.h"
#include "DCFrameScopes.h"
#include "Rules/DCStrikePattern.h"
#include "Voxel/DCVoxelSettings.h"

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
#include "Rules/DCFactionRules.h"
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
	// With -dcmenuflow the pilot starts from the title menu (a real click on SOLO RUN reopens the map with ?solo).
	const bool bMenuFirst = FCString::Strifind(FCommandLine::Get(), TEXT("dcmenuflow=")) != nullptr;
	bAutoStart = UGameplayStatics::HasOption(Options, TEXT("solo")) || (ADCDemoPilot::IsRequested() && !bMenuFirst) || FParse::Param(FCommandLine::Get(), TEXT("dcsolo"))
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
	ReelStaged.Remove(Cast<APlayerController>(Exiting));
	TryRollReel(); // review CP37: the last unstaged machine leaving rolls the clip for everyone else
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
	PendingSpawns.Reset();
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
			City->Build(GS->RunSeed, SectorIndex, MutableGS->CityBuildId, UDCVoxelSettings::Get().Tuning.bAsyncSectorCraft);
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
	RollFactions();
	StartObjective();
	if (GS->SectorIndex == 0)
	{
		// R9: the crew's lives for the whole run, dealt by crew size at the first sector.
		const TArray<int32>& Lives = T.Run.LivesByPlayers;
		GS->CrewLives = Lives.Num() > 0 ? Lives[FMath::Min(FMath::Max(Players().Num(), 1), Lives.Num()) - 1] : 0;
		PendingRedeploy.Reset();
		UE_LOG(LogTemp, Display, TEXT("DCLIVES run start: %d players, %d lives"), Players().Num(), GS->CrewLives);
	}

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
	// -dcdownclient=S knocks down every player but the host (R21: the dying client's own slow motion).
	struct FDownSwitch { const TCHAR* Key; int32 First; int32 Last; const TCHAR* Who; };
	static const FDownSwitch DownSwitches[] = {
		{TEXT("dcdownhost="), 0, 1, TEXT("host")}, {TEXT("dcdownall="), 0, MAX_int32, TEXT("all")}, {TEXT("dcdownclient="), 1, MAX_int32, TEXT("client")}};
	for (const FDownSwitch& Switch : DownSwitches)
	{
		float DownAt = 0.f;
		if (bScriptedDownDone || !FParse::Value(FCommandLine::Get(), Switch.Key, DownAt) || Now < SectorStartTime + DownAt)
		{
			continue;
		}
		bScriptedDownDone = true;
		const TArray<ADCPlayerState*> All = Players();
		for (int32 i = Switch.First; i < All.Num() && i < Switch.Last; ++i)
		{
			if (ADCCharacter* C = All[i]->GetPawn<ADCCharacter>())
			{
				C->HostApplyHurt(10000.f, C->GetActorLocation() + FVector(500, 0, 0));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("DCDOWN scripted %s players knocked down"), Switch.Who);
	}
	CheckWipe();
	if (GetGameState<ADCGameState>()->Phase != EDCRunPhase::Sector)
	{
		return;
	}
	TickWaves(Now);
	TickPressure(Now);
	TickFactions(Now);
	TickRedeploy(Now);
	TickSpawns();
	TickObjective(Now);
	TickPendingCollapses(Now);
	TickScoop(Now);
	TickRogueStrikes(Now);
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (Now < GS->SectorEndTime)
	{
		return;
	}
	EndObjective();
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
	DC_FRAME_SCOPE("Waves");
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
		// R3: a faction's special units only come to maps that faction owns or raids.
		const FDCFactionMix& Mix = GS->FactionMix;
		const bool bAbsent = Def && !Def->Faction.IsNone() && FactionMode() && Def->Faction != Mix.Main && !Mix.Invaders.Contains(Def->Faction);
		if (!Def || bAbsent)
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
	DC_FRAME_SCOPE("Pressure");
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
	const bool bFactions = FactionMode();
	float AliveWeight = 0.f;
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		// R3: every unit that fights the crew counts against the pressure, weighted by its faction (crowds are cheap).
		const bool bCounts = bFactions ? DCFactionRules::FightsCrew(T.Factions, E->GetFaction()) : E->GetKind() == Def->Id;
		const FDCFactionDef* EF = bFactions ? DCFactionRules::Find(T.Factions, E->GetFaction()) : nullptr;
		AliveWeight += E->IsAlive() && bCounts ? (EF ? EF->PressureWeight : 1.f) : 0.f;
	}
	In.Alive = FMath::RoundToInt32(AliveWeight);
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
	if (bFactions)
	{
		// R3: the pack belongs to the map's main faction (big share) or an invader crossing in from the edge.
		const FName Faction = DCFactionRules::PickDropFaction(GS->FactionMix, SectorRng.FRand());
		const bool bInvader = Faction != GS->FactionMix.Main;
		const FDCFactionDef* PackF = DCFactionRules::Find(T.Factions, Faction);
		const int32 Pack = FMath::Max(1, FMath::RoundToInt32(D.PackSize * (PackF ? PackF->PackScale : 1.f)));
		const int32 Landed = SpawnSquad(Faction, Pack, DropPoint(Near, bInvader, Sector.Pressure, bInvader ? Faction : FName()), 400.f);
		LastPackTime = Now;
		UE_LOG(LogTemp, Display, TEXT("DCPACK sector=%d t=%.0f pack=%d (%s%s) landed=%d alive=%d target=%d lag=%.2f photo=%d/%d"), GS->SectorIndex + 1, In.SectorElapsed, D.PackSize,
			*Faction.ToString(), bInvader ? TEXT(" invader") : TEXT(""), Landed, In.Alive, D.TargetAlive, D.Lag, In.PhotoPoints, In.QuotaPoints);
		return;
	}
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
	// Last Frame (R21): slow motion only for the dying player. Solo: the shared clock slows. Co-op: the dying
	// player's own machine slows locally (ADCCharacter::ClientLastFrameSlowMo); everyone else keeps real time.
	const FDCPhotoTuning& P = UDCTuning::Get().Photo;
	if (P.LastFrameSlowMo > 0.f && GetNumPlayers() == 1)
	{
		SetDilation(P.LastFrameSlowMo);
		HitstopEndReal = GetWorld()->GetRealTimeSeconds() + P.LastFrameSlowMoSeconds;
	}
	else if (P.LastFrameSlowMo > 0.f && Player)
	{
		Player->ClientLastFrameSlowMo(P.LastFrameSlowMo, P.LastFrameSlowMoSeconds);
		UE_LOG(LogTemp, Display, TEXT("DCLASTFRAME co-op: local slow motion %.2f for %.1f s sent to %s; shared clock stays %.2f"), P.LastFrameSlowMo, P.LastFrameSlowMoSeconds,
			Player->GetPlayerState() ? *Player->GetPlayerState()->GetPlayerName() : TEXT("?"), GetWorldSettings()->TimeDilation);
	}
	CheckWipe();
}

void ADCGameMode::TickRedeploy(float Now)
{
	// R9 bleed-out: a downed player nobody revived within BleedOutSeconds spends a crew life and redeploys.
	const float BleedOut = UDCTuning::Get().Run.BleedOutSeconds;
	for (ADCPlayerState* PS : Players())
	{
		ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		if (C && C->IsDowned() && !PendingRedeploy.Contains(C) && Now - C->GetDownedAt() >= BleedOut)
		{
			HostRedeploy(C);
		}
	}
}

bool ADCGameMode::HostRedeploy(ADCCharacter* Downed)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!Downed || !GS || GS->CrewLives <= 0 || PendingRedeploy.Contains(Downed))
	{
		return false;
	}
	const FDCRunTuning& Run = UDCTuning::Get().Run;
	// Beside a standing teammate, else the player's own start spot.
	FVector Spot = FVector::ZeroVector;
	bool bBeside = false;
	int32 MyIndex = 0;
	int32 Index = 0;
	for (ADCPlayerState* PS : Players())
	{
		const ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		MyIndex = C == Downed ? Index : MyIndex;
		if (!bBeside && C && C != Downed && !C->IsDowned())
		{
			Spot = C->GetActorLocation() + C->GetActorRightVector() * Run.RedeployBesideCm;
			bBeside = true;
		}
		++Index;
	}
	Spot = bBeside ? Spot : SpawnTransformFor(MyIndex).GetLocation();
	--GS->CrewLives;
	PendingRedeploy.Add(Downed);
	ADCSupplyPod::DropLoad(GetWorld(), Spot, 0, 0, 0.f, 0.f, 0.f); // the drop pod (an empty crate: nothing to pick up)
	UE_LOG(LogTemp, Display, TEXT("DCREDEPLOY %s spends a life (%d left), pod at %s %s"), *Downed->GetName(), GS->CrewLives, *Spot.ToCompactString(), bBeside ? TEXT("beside a teammate") : TEXT("at the start spot"));
	TWeakObjectPtr<ADCCharacter> Who = Downed;
	FTimerHandle Land;
	GetWorldTimerManager().SetTimer(Land, FTimerDelegate::CreateWeakLambda(this, [this, Who, Spot]
	{
		ADCCharacter* C = Who.Get();
		PendingRedeploy.Remove(TWeakObjectPtr<const ADCCharacter>(C));
		const ADCGameState* State = GetGameState<ADCGameState>();
		if (!C || !State || State->Phase != EDCRunPhase::Sector)
		{
			return;
		}
		C->TeleportTo(Spot + FVector(0, 0, 120.f), C->GetActorRotation());
		C->HostRevive();
		C->HostRefill();
		UE_LOG(LogTemp, Display, TEXT("DCREDEPLOY %s is back at %s"), *C->GetName(), *Spot.ToCompactString());
	}), FMath::Max(Run.RedeployFallSeconds, 0.1f), false);
	return true;
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
		// Anyone standing, a solo player still inside the self-revive window, or a redeploy on its way keeps the run alive.
		if (C && (!C->IsDowned() || C->CanSelfRevive() || PendingRedeploy.Contains(C)))
		{
			WipeGraceUntil = -1.f; // someone got up: a later all-down gets its own Last Frame grace
			return;
		}
	}
	// Everyone is down. A downed player still holding a Last Frame (R21) gets a short grace to take it first.
	bool bLastFramePending = false;
	for (ADCPlayerState* PS : Players())
	{
		const ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		bLastFramePending |= C && C->HasLastFrame();
	}
	const float Now = GS->Now();
	if (bLastFramePending && (WipeGraceUntil < 0.f || Now < WipeGraceUntil))
	{
		if (WipeGraceUntil < 0.f)
		{
			WipeGraceUntil = Now + UDCTuning::Get().Photo.LastFrameWipeGraceSeconds;
			FTimerHandle Recheck;
			GetWorldTimerManager().SetTimer(Recheck, FTimerDelegate::CreateWeakLambda(this, [this] { CheckWipe(); }), UDCTuning::Get().Photo.LastFrameWipeGraceSeconds + 0.05f, false);
		}
		return;
	}
	WipeGraceUntil = -1.f;
	// R9: everyone is down but the crew has lives: each downed player redeploys while lives last; no wipe.
	bool bAnyRedeploy = false;
	for (ADCPlayerState* PS : Players())
	{
		ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		bAnyRedeploy |= C && C->IsDowned() && HostRedeploy(C);
	}
	if (bAnyRedeploy)
	{
		return;
	}
	// Everyone is down: the run fails, but the reel still plays (R12).
	UE_LOG(LogTemp, Display, TEXT("DCWIPE all %d players down in sector %d: run failed, rolling the reel"), Players().Num(), GS->SectorIndex + 1);
	GS->bRunFailed = true;
	EndObjective(); // the sector's order fails with the crew (R4: the reel still plays)
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
			Clip.OutfitCode = All[P]->OutfitCode;
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
	ReelStaged.Reset();
	// Async restage (review CP34): the clock is held until the host's reel director has the clip's city in (it sets
	// the real start); ReelRestageMaxSeconds is the fallback. Synchronous: a beat for every machine to re-stage.
	const bool bHold = UDCVoxelSettings::Get().Tuning.bAsyncReelRestage;
	GS->bReelClipLive = !bHold;
	GS->ReelClipStart = GS->Now() + (bHold ? UDCTuning::Get().Run.ReelRestageMaxSeconds : 0.3f);
	ReelStageReal = GetWorld()->GetRealTimeSeconds();
	SetDilation(UDCTuning::Get().Run.ReelSlowMo);
	GS->ForceNetUpdate();
}

void ADCGameMode::NoteReelStaged(APlayerController* PC, int32 ClipIndex)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!GS || !PC || GS->Phase != EDCRunPhase::Reel || GS->ReelIndex != ClipIndex || GS->bReelClipLive)
	{
		return;
	}
	ReelStaged.Add(PC);
	TryRollReel();
}

void ADCGameMode::TryRollReel()
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!GS || GS->Phase != EDCRunPhase::Reel || !GS->ReelClips.IsValidIndex(GS->ReelIndex) || GS->bReelClipLive || ReelStaged.Num() == 0)
	{
		return;
	}
	for (auto It = ReelStaged.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent(); // review CP36: a disconnected machine's entry never counts
		}
	}
	if (ReelStaged.Num() < GetNumPlayers())
	{
		return; // another machine is still restaging (the ReelRestageMaxSeconds cap rolls it anyway)
	}
	GS->ReelClipStart = FMath::Min(GS->ReelClipStart, GS->Now() + 0.3f);
	GS->bReelClipLive = true;
	GS->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("DCREEL all %d players staged clip=%d: rolling"), ReelStaged.Num(), GS->ReelIndex);
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
	ADCGameState* GS = GetGameState<ADCGameState>();
	if (!GS->bReelClipLive && Now >= GS->ReelClipStart)
	{
		GS->bReelClipLive = true; // the hold ran out (no director here, or a stuck restage): play from the cap
		GS->ForceNetUpdate();
	}
	if (Now >= GS->ReelClipStart + UDCTuning::Get().Run.ClipSeconds)
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
	const int32 Extracted = GS->ObjectiveResults.FilterByPredicate([](uint8 b) { return b != 0; }).Num();
	GS->Subhead = FString::Printf(TEXT("%s  |  %d PHOTOGRAPHER%s  |  %s PROPAGANDA POINTS RAISED  |  QUOTAS MET %d/%d  |  HQ ORDERS EXTRACTED %d/%d  |  RUN SEED %d"),
		GS->bRunFailed ? TEXT("UNIT LOST IN ACTION, REEL RECOVERED") : TEXT("ALL SECTORS LIBERATED"),
		All.Num(), All.Num() == 1 ? TEXT("") : TEXT("S"), *FText::AsNumber(Total).ToString(), QuotasMet, GS->QuotaResults.Num(), Extracted, GS->ObjectiveResults.Num(), GS->RunSeed);
	GS->ForceNetUpdate();
}

// ---------------------------------------------------------------- strikes, hitstop

void ADCGameMode::Tick(float DeltaSeconds)
{
	DC_FRAME_SCOPE("GameMode");
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

namespace
{
	// Call effects by id (R7): a stratagem row's Effect picks how the call lands. A new kind is a new entry.
	using FCallEffectFn = bool (*)(ADCGameMode&, const FDCStratagemDef&, const FVector&, AController*);
	bool EffectSupply(ADCGameMode& GM, const FDCStratagemDef& Def, const FVector& Target, AController* Caller)
	{
		ADCSupplyPod::DropLoad(GM.GetWorld(), Target, Def.SupplyFilm, Def.SupplyAmmo, Def.SupplyHealth, Def.SupplyCrewRadiusCm, Def.TelegraphSeconds);
		UE_LOG(LogTemp, Display, TEXT("DCSUPPLY %s called at %s"), *Def.Id.ToString(), *Target.ToCompactString());
		return true;
	}
	const TMap<FName, FCallEffectFn>& CallEffects()
	{
		static const TMap<FName, FCallEffectFn> Map = {
			{TEXT("Supply"), &EffectSupply},
		};
		return Map;
	}
}

void ADCGameMode::CallStrike(const FDCStratagemDef& Def, const FVector& Target, AController* Caller)
{
	if (const FCallEffectFn* Effect = CallEffects().Find(Def.Effect))
	{
		(*Effect)(*this, Def, Target, Caller);
		return;
	}
	const ADCGameState* GS = GetGameState<ADCGameState>();
	FRandomStream Rng(GetTypeHash(Target) ^ GS->RunSeed);
	// R12: the blast layout is a registry entry named by the stratagem row (Pattern, Rules/DCStrikePattern.h).
	const APawn* CallerPawn = Caller ? Caller->GetPawn() : nullptr;
	const FVector2D Along = CallerPawn ? FVector2D(Target - CallerPawn->GetActorLocation()).GetSafeNormal() : FVector2D(1.f, 0.f);
	for (int32 i = 0; i < FMath::Max(Def.Blasts, 1); ++i)
	{
		const DCStrikePattern::FLayout Layout{i, FMath::Max(Def.Blasts, 1), Def.BlastScatterCm, Along.IsNearlyZero() ? FVector2D(1.f, 0.f) : Along};
		const FVector2D Scatter = DCStrikePattern::Offset(Def.Pattern, Layout, Rng);
		const FTransform Xf(FVector(Target.X + Scatter.X, Target.Y + Scatter.Y, Target.Z));
		ADCOrbitalStrike* Strike = GetWorld()->SpawnActorDeferred<ADCOrbitalStrike>(ADCOrbitalStrike::StaticClass(), Xf);
		Strike->Init(Def.Id, GS->Now() + Def.TelegraphSeconds + i * Def.BlastSpacing, Caller);
		Strike->FinishSpawning(Xf);
	}
}
