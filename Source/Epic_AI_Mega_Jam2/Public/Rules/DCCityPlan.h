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

/** A voxel-forge model placed in the city (R16): a street prop, plaza dressing or a wreck in a rubble zone. */
USTRUCT()
struct FDCPropPlan
{
	GENERATED_BODY()
	UPROPERTY() FName Model;
	/** Sector-local cm, on the street surface. */
	UPROPERTY() FVector Location = FVector::ZeroVector;
	/** Quarter turns (0-3), so the voxels stay on the grid. */
	UPROPERTY() int32 Turns = 0;
};

USTRUCT()
struct FDCCityPlan
{
	GENERATED_BODY()
	UPROPERTY() TArray<FDCLotPlan> Buildings;
	/** District builder (R16): open lots, pre-ruined lots, the landmark's index in Buildings, props, and the sight
	 *  line kept open from the players' spawn street to the landmark. */
	UPROPERTY() TArray<FVector> Plazas;
	UPROPERTY() TArray<FVector> RubbleZones;
	UPROPERTY() TArray<FDCPropPlan> Props;
	UPROPERTY() int32 Landmark = -1;
	UPROPERTY() FVector SightFrom = FVector::ZeroVector;
	UPROPERTY() FVector SightTo = FVector::ZeroVector;
	/** Kind of every lot of the grid (row-major), for previews and tests. */
	UPROPERTY() TArray<FName> LotKinds;
	UPROPERTY() int32 GridX = 0;
	UPROPERTY() int32 GridY = 0;
	UPROPERTY() float LotSizeCm = 0.f;
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
