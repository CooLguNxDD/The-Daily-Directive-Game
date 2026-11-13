// Pure seeded city layout: which lots hold towers, how tall, where players spawn. Same seed, same city.
#pragma once

#include "CoreMinimal.h"
#include "DCCityPlan.generated.h"

struct FDCCityTuning;

USTRUCT(BlueprintType)
struct FDCBuildingSpec
{
	GENERATED_BODY()
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() int32 SizeX = 3;
	UPROPERTY() int32 SizeY = 3;
	UPROPERTY() int32 Floors = 8;
	UPROPERTY() float BlockSize = 300.f;
	UPROPERTY() uint8 ColorIndex = 0;
	/** Voxel city: index of the building recipe (DCVoxelSettings Recipes) this lot is crafted from. */
	UPROPERTY() uint8 Recipe = 0;
};

USTRUCT()
struct FDCLotPlan
{
	GENERATED_BODY()
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() FDCBuildingSpec Spec;
};

USTRUCT()
struct FDCCityPlan
{
	GENERATED_BODY()
	UPROPERTY() TArray<FDCLotPlan> Buildings;
	/** Street intersections, used for player spawns, enemy spawns and supply pods. */
	UPROPERTY() TArray<FVector> StreetPoints;
	UPROPERTY() float HalfExtent = 0.f;
};

namespace DCCityRules
{
	/** Sector seed from the run seed: each sector differs, every machine agrees. */
	EPIC_AI_MEGA_JAM2_API int32 SectorSeed(int32 RunSeed, int32 SectorIndex);

	EPIC_AI_MEGA_JAM2_API FDCCityPlan PlanCity(int32 Seed, const FDCCityTuning& Tuning, int32 NumColors);
}
