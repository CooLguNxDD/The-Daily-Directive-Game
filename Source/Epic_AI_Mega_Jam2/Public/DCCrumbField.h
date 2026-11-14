// R26 crumb tier (advisor CP38 plan): the small stuff a break throws off. Single voxels and 2-3 voxel clusters fly
// ballistically (no Chaos body), bounce once or twice on the street, lie a while and shrink away. One instanced cube
// component per colour bucket, so hundreds of crumbs cost a handful of draw calls. Local cosmetics only: the host's
// Rest op owns the rubble that stays; crumbs never block or score.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DCCrumbField.generated.h"

class UInstancedStaticMeshComponent;

/** One crumb to throw: world position, velocity, edge (cm), colour and glow (palette alpha as stops). */
struct FDCCrumbSpawn
{
	FVector At = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float SizeCm = 10.f;
	FLinearColor Color = FLinearColor::Gray;
	float Glow = 0.f;
};

UCLASS(NotBlueprintable)
class EPIC_AI_MEGA_JAM2_API UDCCrumbField : public USceneComponent
{
	GENERATED_BODY()

public:
	UDCCrumbField();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Street height under a point (the voxel city's column tops); crumbs bounce on it. */
	TFunction<float(const FVector&)> GroundZ;

	/** Throws one crumb; past CrumbMax the oldest is recycled. */
	void Spawn(const FDCCrumbSpawn& S);
	/** Drops every crumb (sector change, reel restage). */
	void ClearAll();
	int32 LiveCount() const { return Live; }

private:
	struct FCrumb
	{
		int32 Bucket = INDEX_NONE;
		int32 Instance = INDEX_NONE;
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FVector SpinAxis = FVector::UpVector;
		float SpinDegS = 0.f;
		float AngleDeg = 0.f;
		float Size = 10.f;
		float Born = 0.f;
		float Life = 6.f;
		int32 Bounces = 0;
		bool bResting = false;
		bool bAlive = false;
	};
	struct FBucket
	{
		TObjectPtr<UInstancedStaticMeshComponent> Ism;
		TArray<int32> FreeInstances;
		bool bDirty = false;
	};
	int32 BucketFor(const FLinearColor& Color, float Glow);
	void Kill(int32 Index);

	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Isms;
	TArray<FBucket> Buckets;
	TMap<uint32, int32> BucketByKey;
	TArray<FCrumb> Crumbs;
	TArray<int32> FreeCrumbs;
	int32 Live = 0;
	float Clock = 0.f;
	FRandomStream Rng = FRandomStream(0xC0B5);
};
