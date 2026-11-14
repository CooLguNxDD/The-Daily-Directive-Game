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

/** A voxel-forge model fixed to a building (R2 props): roof clutter or a wall mount. Crafted into the tower's own grid,
 *  so it falls and shatters with the building through the same voxel ops. */
USTRUCT()
struct FDCAttachPlan
{
	GENERATED_BODY()
	UPROPERTY() FName Model;
	/** Prop layer it came from (tests and previews count props per layer). */
	UPROPERTY() FName Layer;
	/** Mount id from the mount registry (DCVoxelCraft.cpp): Roof or Wall. */
	UPROPERTY() FName Mount;
	/** Face 0..3 (-X, +X, -Y, +Y): which facade a wall mount sits on, or the way roof clutter faces. */
	UPROPERTY() int32 Face = 0;
	/** 0..1 across the roof deck (U, V) or along the face (U); V is the mount height in floors for a wall. */
	UPROPERTY() float U = 0.5f;
	UPROPERTY() float V = 0.5f;
};

USTRUCT()
struct FDCLotPlan
{
	GENERATED_BODY()
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() FDCBuildingSpec Spec;
	UPROPERTY() TArray<FDCAttachPlan> Attach;
};

/** A line strung across a street (R2 props): sector-local cm end points, sag at mid span, an optional lit bead every
 *  BeadEveryCm and optional poles under both ends. Crafted into the prop grid. */
USTRUCT()
struct FDCCablePlan
{
	GENERATED_BODY()
	UPROPERTY() FVector A = FVector::ZeroVector;
	UPROPERTY() FVector B = FVector::ZeroVector;
	UPROPERTY() float SagCm = 0.f;
	UPROPERTY() FName Material;
	UPROPERTY() FName BeadMaterial;
	UPROPERTY() float BeadEveryCm = 0.f;
	UPROPERTY() FName PoleMaterial;
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
	/** Prop layer it came from (R2: tests count props per layer). */
	UPROPERTY() FName Layer;
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
	UPROPERTY() TArray<FDCCablePlan> Cables;
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
