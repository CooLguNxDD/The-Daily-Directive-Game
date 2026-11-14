// Sector objectives and extraction (R4), pure: no world. HQ hands each sector one objective from data (+Objectives
// rows, picked by the run seed): Retrieve (carry a package out of the deep swarm) or Destroy (bring down marked
// towers; strikes, carving and faction fire all count). Done objectives open an extraction beacon; reaching it ends
// the sector. A sector that runs out of time fails its objective but still goes on (and the reel always plays).
// Stages move through a per-kind transition table, never a ladder.
#pragma once

#include "CoreMinimal.h"
#include "DCObjectiveRules.generated.h"

UENUM(BlueprintType)
enum class EDCObjectiveStage : uint8
{
	None,
	/** Get the package / bring the targets down. */
	Active,
	/** The beacon is lit: get to it (the carrier, for Retrieve). */
	Extract,
	Done,
	Failed,
};

USTRUCT(BlueprintType)
struct FDCObjectiveDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FName Id;
	/** Rule kind (transition table): Retrieve or Destroy. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FName Kind;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float Weight = 1.f;
	/** HUD line while active ({N} = targets left) and the HQ brief on the sector card. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FString Title;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FString Brief;
	/** Destroy: marked towers to bring down. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") int32 Targets = 3;
	/** Retrieve: a standing player this close picks the package up. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float PickupRadiusCm = 350.f;
	/** Retrieve: the package lands on a seeded street point among the DeepFraction farthest from the crew's spawn. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float DeepFraction = 0.25f;
	/** Extraction: the beacon's zone, and how far from the objective it lands at least. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float ExtractRadiusCm = 900.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float ExtractMinDistanceCm = 6000.f;
	/** Retrieve: the deep swarm. A main-faction squad of GuardSquad sits on the package, topped up every GuardGapSeconds
	 *  while nobody carries it. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") int32 GuardSquad = 8;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float GuardGapSeconds = 30.f;
	/** At most this many guard squads per sector (advisor a-7: loitering by the package must not pile up units). */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") int32 GuardWaves = 3;
	/** Propaganda Points paid to every player on extraction; the sector ends ExtractEndSeconds later. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") int32 RewardPoints = 600;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float ExtractEndSeconds = 2.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FString ExtractToast;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FString DoneToast;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FString FailToast;
};

/** How the objective reads in the world (one Look row): signal-yellow beacon, target pillars and the package. */
USTRUCT(BlueprintType)
struct FDCObjectiveLook
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FLinearColor Color = FLinearColor(0.807f, 1.f, 0.042f);
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float BeaconHeightCm = 6000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float BeaconWidthCm = 120.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float BeaconGlow = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float BeaconCandela = 4000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float BeaconLightRadiusCm = 1600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float MarkerHeightCm = 2500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float MarkerWidthCm = 40.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float MarkerGlow = 2.f;
	/** Pillar base above the tower's roof line (cm), so the marker reads over the skyline. */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float MarkerLiftCm = 4000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FName PackageModel = TEXT("package");
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float PackageScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float PackageCandela = 600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Objective") float PackageLightRadiusCm = 700.f;
	/** On the carrier: relative to the carrier's location (on the back). */
	UPROPERTY(EditAnywhere, Config, Category = "Objective") FVector CarryOffset = FVector(-40.f, 0.f, 40.f);
};

/** What the host measured this tick. */
struct FDCObjectiveInputs
{
	bool bCarried = false;
	int32 Destroyed = 0;
	int32 Targets = 0;
	/** The carrier (Retrieve) or any standing player (Destroy) stands in the beacon's zone. */
	bool bCarrierInZone = false;
	bool bAnyoneInZone = false;
	bool bTimeUp = false;
};

namespace DCObjectiveRules
{
	/** Seeded weighted pick of a sector's objective (nullptr when there are none). */
	EPIC_AI_MEGA_JAM2_API const FDCObjectiveDef* Pick(const TArray<FDCObjectiveDef>& Defs, int32 Seed, int32 Sector);
	/** The stage after this tick. Done and Failed are final. */
	EPIC_AI_MEGA_JAM2_API EDCObjectiveStage Next(FName Kind, EDCObjectiveStage Stage, const FDCObjectiveInputs& In);
	/** Seeded pick of Count marked towers among Candidates (positions): far from From first, never two closer than
	 *  MinApartCm (relaxed when the map is small). Returns indices into Candidates. */
	EPIC_AI_MEGA_JAM2_API TArray<int32> PickTargets(const TArray<FVector>& Candidates, const FVector& From, int32 Count, float MinApartCm, int32 Seed);
	/** Index of the point farthest from From (deep in the swarm); INDEX_NONE when empty. */
	EPIC_AI_MEGA_JAM2_API int32 Farthest(const TArray<FVector>& Points, const FVector& From);
	/** Seeded pick among the farthest Fraction of Points from From (deep, but not always the same corner). */
	EPIC_AI_MEGA_JAM2_API int32 Deep(const TArray<FVector>& Points, const FVector& From, float Fraction, int32 Seed);
	/** Index of the point nearest MinDistance away from From (at least MinDistance when any is); INDEX_NONE when empty. */
	EPIC_AI_MEGA_JAM2_API int32 AtLeast(const TArray<FVector>& Points, const FVector& From, float MinDistance);
}
