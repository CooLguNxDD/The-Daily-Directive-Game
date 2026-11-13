#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DCTypes.h"
#include "DCGameState.generated.h"

class ADCEnvironment;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ADCGameState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Host only: records a detonation every machine can read for scoring and tags. */
	int32 AddBlast(const FVector& Location, float Radius, FName StratagemId);

	const FDCBlastRecord* FindBlast(int32 Id) const;

	/** Synced clock: host time on every machine. */
	float Now() const { return static_cast<float>(GetServerWorldTimeSeconds()); }

	ADCEnvironment* GetEnvironment() const { return Environment; }

	UPROPERTY(Replicated) TArray<FDCBlastRecord> Blasts;
	UPROPERTY(Replicated) EDCRunPhase Phase = EDCRunPhase::Lobby;
	UPROPERTY(Replicated) int32 SectorIndex = 0;
	UPROPERTY(Replicated) float SectorEndTime = 0.f;
	UPROPERTY(Replicated) int32 RunSeed = 0;
	/** Server time the current timed phase (draft, final cut, vote) ends. */
	UPROPERTY(Replicated) float PhaseEndTime = 0.f;
	UPROPERTY(Replicated) bool bRunFailed = false;
	UPROPERTY(Replicated) TArray<FDCCollapseRecord> Collapses;
	UPROPERTY(Replicated) FDCScoopState Scoop;
	/** City Mutators drafted this run (apply to everyone). */
	UPROPERTY(Replicated) TArray<FName> Mutators;

	// Reel (R13): the host steps through clips; every machine re-stages the current one.
	UPROPERTY(Replicated) TArray<FDCReelClip> ReelClips;
	UPROPERTY(Replicated) int32 ReelIndex = -1;
	/** 0 = playing (slow motion), 1 = frozen at the shutter, 2 = showing the photo. */
	UPROPERTY(Replicated) uint8 ReelStage = 0;
	/** Server time when the current clip started playing. */
	UPROPERTY(Replicated) float ReelClipStart = 0.f;

	// Front page (R15).
	UPROPERTY(Replicated) TArray<FDCPagePick> PagePicks;
	UPROPERTY(Replicated) int32 HeadlineIndex = -1;
	UPROPERTY(Replicated) FString Headline;
	UPROPERTY(Replicated) FString Subhead;

	/** Host only (not replicated): where decoy beacons lure enemies, with their expiry time. */
	TArray<TPair<FVector, float>> Lures;

	bool HasMutator(FName Id) const { return Mutators.Contains(Id); }
	void AddCollapse(int32 Sector, int32 Lot, const FVector& Epicenter);

private:
	UPROPERTY() TObjectPtr<ADCEnvironment> Environment;
	int32 NextBlastId = 1;
};
