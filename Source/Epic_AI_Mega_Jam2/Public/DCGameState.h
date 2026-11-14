#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DCTypes.h"
#include "Voxel/DCVoxelTypes.h"
#include "DCGameState.generated.h"

class ADCEnvironment;
class ADCVoxelCity;

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
	/** Host only: records an event that peaks at PeakTime (server clock), telegraphed from now. */
	int32 AddEvent(FName Kind, const FVector& Location, float Radius, float PeakTime, int32 LotIndex);

	const FDCBlastRecord* FindBlast(int32 Id) const;

	/** Synced clock: host time on every machine. */
	float Now() const { return static_cast<float>(GetServerWorldTimeSeconds()); }

	ADCEnvironment* GetEnvironment() const { return Environment; }
	ADCVoxelCity* GetVoxelCity() const { return VoxelCity; }

	/** Host: appends a voxel op for the current city build (Seq and Build filled in) and returns it. */
	/** Appends a voxel op (host). ApplyTime: when every city commits it (a strike's detonation); default now. */
	FDCVoxelOp& AddVoxelOp(const FDCVoxelOp& Op, float ApplyTime = -1.f);

	/** Voxel city (R1-R3): which sector is built (-1 = none) and the build serial; every machine crafts from these. */
	UPROPERTY(Replicated) int32 CitySector = -1;
	UPROPERTY(Replicated) int32 CityBuildId = 0;
	/** The host's voxel ops for the whole run, applied in Seq order by every machine (and replayed by the reel). */
	UPROPERTY(Replicated) TArray<FDCVoxelOp> VoxelOps;

	UPROPERTY(Replicated) TArray<FDCBlastRecord> Blasts;
	UPROPERTY(Replicated) EDCRunPhase Phase = EDCRunPhase::Lobby;
	UPROPERTY(Replicated) int32 SectorIndex = 0;
	UPROPERTY(Replicated) float SectorEndTime = 0.f;
	/** Server time the sector began: with the run seed it drives the weather arc on every machine (R18). */
	UPROPERTY(Replicated) float SectorStartTime = 0.f;
	UPROPERTY(Replicated) int32 RunSeed = 0;
	/** Server time the current timed phase (draft, final cut, vote) ends. */
	UPROPERTY(Replicated) float PhaseEndTime = 0.f;
	UPROPERTY(Replicated) bool bRunFailed = false;
	/** Photo score filed by the whole crew this sector, against the sector quota. */
	UPROPERTY(Replicated) int32 SectorPhotoPoints = 0;
	/** One entry per finished sector: was the quota met. */
	UPROPERTY(Replicated) TArray<bool> QuotaResults;
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
	/** False while the host holds the clip clock for its restage (review CP34): ReelClipStart is then only the
	 *  fallback cap; the host's reel director sets the real start and this flag once its city is in. */
	UPROPERTY(Replicated) bool bReelClipLive = true;

	// Front page (R15).
	UPROPERTY(Replicated) TArray<FDCPagePick> PagePicks;
	UPROPERTY(Replicated) int32 HeadlineIndex = -1;
	UPROPERTY(Replicated) FString Headline;
	UPROPERTY(Replicated) FString Subhead;

	/** Host only (not replicated): where decoy beacons lure enemies, with their expiry time. */
	TArray<TPair<FVector, float>> Lures;

	bool HasMutator(FName Id) const { return Mutators.Contains(Id); }
	/** The active mutators' definitions (data drives their effects; code never names a mutator). */
	TArray<const struct FDCUpgradeDef*> ActiveMutators() const;
	void AddCollapse(int32 Sector, int32 Lot, const FVector& Epicenter, float Time = -1.f);

private:
	UPROPERTY() TObjectPtr<ADCEnvironment> Environment;
	UPROPERTY() TObjectPtr<ADCVoxelCity> VoxelCity;
	int32 NextVoxelSeq = 1;
	int32 NextBlastId = 1;
};
