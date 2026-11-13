// Decay effects for the voxel city (R4): dust, smoke, fire and embers as chunky voxel puffs (one instanced mesh per
// effect kind, a fixed pool, no allocation per puff). Kinds and emitters are data rows (DCVoxelSettings Fx / Emitters);
// code only names the moment (crater, landing, burning rubble). Local and cosmetic on every machine.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Voxel/DCVoxelTypes.h"
#include "DCVoxelFx.generated.h"

class UInstancedStaticMeshComponent;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCVoxelFx : public AActor
{
	GENERATED_BODY()

public:
	ADCVoxelFx();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Plays an emitter row by id at a point (Radius spreads the spawn area). */
	void Emit(FName Emitter, const FVector& Where, float Radius);
	/** Keeps an emitter row going at a point for Seconds (smoke and fire over the rubble in the decay window). */
	void Burn(FName Emitter, const FVector& Where, float Radius, float Seconds);
	void ClearAll();

private:
	struct FPuff
	{
		int32 Kind = 0;
		FVector Pos;
		FVector Vel;
		FRotator Spin;
		FRotator SpinRate;
		float Born = 0.f;
		float Life = 1.f;
	};
	struct FBurn
	{
		FName Emitter;
		FVector Where;
		float Radius = 0.f;
		float Until = 0.f;
		float Next = 0.f;
	};

	int32 KindIndex(FName Kind) const;
	void Spawn(int32 Kind, const FVector& Where, float Radius, int32 Count);

	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Meshes;
	TArray<TArray<FPuff>> Puffs;
	TArray<FBurn> Burns;
	FRandomStream Rng;
};
