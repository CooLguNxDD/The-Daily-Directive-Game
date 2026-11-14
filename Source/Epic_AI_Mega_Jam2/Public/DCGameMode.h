// Host-only run director: seeds the run, builds each sector's city, spawns waves, calls strikes, books photo
// scores and moves the run through its phases: lobby -> sector -> draft -> sector -> draft -> sector -> reel ->
// final cut -> vote (co-op) -> front page. Each phase has an enter and a tick handler, looked up in a table.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DCTypes.h"
#include "Rules/DCCityPlan.h"
#include "DCGameMode.generated.h"

class ADCCharacter;
class ADCBuilding;
class ADCPlayerState;
struct FDCStratagemDef;
struct FDCPressureTuning;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** A player's machine reports the current reel clip staged (ServerReelStaged). */
	void NoteReelStaged(APlayerController* PC, int32 ClipIndex);
	ADCGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	void CallStrike(const FDCStratagemDef& Def, const FVector& Target, AController* Caller);
	/** Packaged-build smoke (-dcsmoke): start, strike a tower, check the collapse, log DCSMOKE, quit. */
	void StartSmoke();
	void OnPhotoGraded(ADCCharacter* Photographer, const FDCPhotoRecord& Record, const FDCPhotoInputs& Inputs, TArray<FDCPoseSample>&& Poses);
	void OnPlayerDowned(ADCCharacter* Player);
	/** Fails the run when every player is down and nobody can still get up (R12). */
	void CheckWipe();
	void OnBuildingCollapsed(ADCBuilding* Building);
	/** Every collapse goes through here. Delay > 0 writes a telegraphed event record (Kind) now and collapses the
	 *  tower at its peak on the server clock; Delay 0 collapses at once (a strike's own record covers it). */
	void ScheduleCollapse(ADCBuilding* Building, const FVector& Epicenter, FName Kind, float Delay, float AtTime = -1.f);
	void RequestHitstop();
	bool IsCombatLive() const;

	// Player intent, validated here (called from ADCPlayerController server RPCs).
	void StartRun();
	void RestartRun();
	void PickUpgrade(ADCPlayerState* PS, FName UpgradeId);
	void SubmitFinalCut(ADCPlayerState* PS, const TArray<int32>& PhotoIds);
	void CastVote(ADCPlayerState* PS, int32 PhotoId);

	const FDCCityPlan& GetCityPlan() const { return CityPlan; }
	/** Seconds between rogue strikes from the active mutators (0 = none). */
	float RogueInterval() const;

private:
	using FPhaseFn = void (ADCGameMode::*)(float Now);
	struct FPhaseHandlers
	{
		FPhaseFn Enter = nullptr;
		FPhaseFn Tick = nullptr;
	};
	static const TMap<EDCRunPhase, FPhaseHandlers>& PhaseTable();
	void EnterPhase(EDCRunPhase Phase);

	void EnterSector(float Now);
	void TickSectorPhase(float Now);
	void EnterDraft(float Now);
	void TickDraft(float Now);
	void EnterReel(float Now);
	void TickReel(float Now);
	void TickReelPlay(float Now);
	void TickReelFreeze(float Now);
	void TickReelPhoto(float Now);
	void SetReelStage(uint8 Stage, float Dilation);
	void EnterFinalCut(float Now);
	void TickFinalCut(float Now);
	void EnterVote(float Now);
	void TickVote(float Now);
	void EnterFrontPage(float Now);

	void BuildCity(int32 SectorIndex);
	void ClearCombat();
	void ClearCity();
	void TickWaves(float Now);
	void TickScoop(float Now);
	void TickRogueStrikes(float Now);
	void TickPendingCollapses(float Now);
	void BeginReelClip(int32 Index);
	/** Machines whose reel has the current clip's city in (advisor CP33); the clip rolls when all players are in. */
	TSet<TWeakObjectPtr<APlayerController>> ReelStaged;
	/** Rolls the current reel clip once every connected player's machine is staged (after a report or a logout). */
	void TryRollReel();
	void SetDilation(float Value);
	FTransform SpawnTransformFor(int32 PlayerIndex) const;
	TArray<ADCPlayerState*> Players() const;

	FDCCityPlan CityPlan;
	int32 RequestedSeed = 0;
	bool bAutoStart = false;
	int32 AutoStartPlayers = 1;
	bool bScriptedDownDone = false;
	double HitstopEndReal = 0.0;
	/** Server time a wipe waits until, while a downed player can still take a Last Frame (-1 = none). */
	float WipeGraceUntil = -1.f;
	float SectorStartTime = 0.f;
	TArray<int32> WaveSpawned;
	TArray<float> WaveNextTime;
	float LastPackTime = -1e6f;
	float LastDownTime = -1e6f;
	void TickPressure(float Now);
	// R3 faction director (DCFactionDirector.cpp): the map's mix, squads, the friendly army and faction strikes.
	void RollFactions();
	void TickFactions(float Now);
	/** Spawns Count units of a faction around Centre as one squad (formation slots); returns how many landed. */
	int32 SpawnSquad(FName Faction, int32 Count, const FVector& Centre, float Spread);
	/** A street point for a drop: ring around a standing player (main faction, allies) or the far edge (invaders). */
	FVector DropPoint(const FVector& Near, bool bFar, const FDCPressureTuning& P, FName DropFaction = NAME_None);
	bool FactionMode() const;
	float NextAllyTime = 0.f;
	float NextFactionStrike = 0.f;
	TMap<FName, float> FactionStrikeReady;
	int32 NextSquadId = 1;
	float NextTerritoryTime = 0.f;
	/** Rolled spawns waiting their frame (advisor a-3: a squad drop is N model meshes; at most FactionMix.SpawnsPerFrame
	 *  start per tick). Rolls happen at queue time, so the seeded sequence is unchanged. */
	struct FPendingSpawn
	{
		FName Kind;
		FTransform Xf;
		int32 Squad = 0;
		int32 Slot = 0;
		int32 Size = 1;
	};
	TArray<FPendingSpawn> PendingSpawns;
	void TickSpawns();
	// R4 objective director (DCObjectiveDirector.cpp).
	void StartObjective();
	void TickObjective(float Now);
	/** Sector end: a live objective fails (time up). */
	void EndObjective();
	UPROPERTY() TObjectPtr<class ADCObjective> Objective;
	TArray<TWeakObjectPtr<ADCBuilding>> ObjectiveTargets;
	float NextGuardTime = 0.f;
	FRandomStream SectorRng;
	float NextScoopTime = 0.f;
	int32 LastScoop = -1;
	float NextRogueTime = 0.f;
	double ReelStageReal = 0.0;
	double NextHeartbeatReal = 0.0;
	/** Host-only pose buffers per photo (never replicated until the reel picks the shot). */
	TMap<int32, TArray<FDCPoseSample>> PhotoPoses;
	/** Domino Condemnation: chain generation per collapsed building. */
	TMap<TObjectKey<ADCBuilding>, int32> DominoGeneration;
	struct FPendingCollapse
	{
		TWeakObjectPtr<ADCBuilding> Building;
		FVector Epicenter = FVector::ZeroVector;
		float At = 0.f;
	};
	TArray<FPendingCollapse> PendingCollapses;
};
