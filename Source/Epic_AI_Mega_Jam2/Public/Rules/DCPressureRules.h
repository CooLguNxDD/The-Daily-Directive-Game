// Pure swarm-pressure rule (R6): no world, no engine objects. The host feeds it the sector clock, the crew's photo
// score against HQ's quota and what is alive; it answers whether a pack of swarmers drops now and how big.
// Lagging behind the quota raises the pressure; only photo (and scoop) points lower it, never kills. A collapse or a
// downed player buys a short relief, and a hard cap keeps a lagging crew out of a death spiral.
#pragma once

#include "CoreMinimal.h"
#include "DCPressureRules.generated.h"

/** Per-sector pressure tuning (a Pressure=(...) block on each +Sectors= row in Config/DefaultGame.ini). */
USTRUCT(BlueprintType)
struct FDCPressureTuning
{
	GENERATED_BODY()
	/** Enemy kind the packs are made of. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") FName EnemyId = TEXT("Swarmer");
	/** Seconds into the sector before the first pack. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float StartSeconds = 25.f;
	/** Swarmers alive the crew faces when it keeps pace with the quota (solo). */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 BaseAlive = 5;
	/** Extra alive per player beyond the first. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 PerPlayer = 3;
	/** Extra alive at full quota lag. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 LagBonus = 8;
	/** Lag is full when the crew is this fraction of the quota behind the expected pace. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float FullLagFraction = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 PackMin = 5;
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 PackMax = 8;
	/** Seconds between packs at no lag; full lag halves it. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float PackGapSeconds = 14.f;
	/** No pack within this long after a collapse (a strike buys a photo window) or after a player went down. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float ReliefSeconds = 5.f;
	/** Never more swarmers alive than this, whatever the lag. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") int32 HardCap = 20;
	/** Packs land this far from a standing player (street points in the ring), out of the line of fire. */
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float RingMinCm = 3500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Pressure") float RingMaxCm = 7000.f;
};

/** What the host knows this tick. */
struct FDCPressureInputs
{
	float SectorElapsed = 0.f;
	float SectorDuration = 240.f;
	int32 PhotoPoints = 0;
	int32 QuotaPoints = 0;
	int32 PlayersStanding = 1;
	int32 Alive = 0;
	float SinceCollapse = 1e6f;
	float SinceDown = 1e6f;
	float SinceLastPack = 1e6f;
};

struct FDCPressureDecision
{
	/** 0..1: how far behind HQ's pace the crew is. */
	float Lag = 0.f;
	int32 TargetAlive = 0;
	/** Swarmers to drop now (0 = no pack this tick). */
	int32 PackSize = 0;
};

namespace DCPressureRules
{
	EPIC_AI_MEGA_JAM2_API float QuotaLag(const FDCPressureInputs& In, const FDCPressureTuning& T);
	EPIC_AI_MEGA_JAM2_API FDCPressureDecision Decide(const FDCPressureInputs& In, const FDCPressureTuning& T);
}
