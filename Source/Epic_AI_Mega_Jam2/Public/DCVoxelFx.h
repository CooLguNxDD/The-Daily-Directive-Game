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
	/** Spawns Count puffs of a kind by id (weather precipitation around the camera). */
	void SpawnKind(FName Kind, const FVector& Where, float Radius, int32 Count);
	/** R3 faction fire: a thin glowing bolt from From to To for a few frames (pooled, unlit, faction cue colour). */
	void Tracer(const FVector& From, const FVector& To, const FLinearColor& Color, float Glow);
	/** Wind (cm/s) every puff drifts with (weather, R18). */
	void SetWind(const FVector& InWind) { Wind = InWind; }

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
	struct FBolt
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		float Until = 0.f;
	};
	TArray<FBolt> Bolts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BoltMeshes;
	UPROPERTY() TMap<uint32, TObjectPtr<class UMaterialInstanceDynamic>> BoltMaterials;
	int32 NextBolt = 0;
	TArray<TArray<FPuff>> Puffs;
	/** The fx timeline: world delta scaled by the presentation clock (puff birth, life and burns use it). */
	float FxTime = 0.f;
	/** Instances written last frame per kind: a frame only touches live puffs plus the ones that just died, so the
	 *  cost follows the live count, never the pool size (a 1,900-instance pool rewritten each frame cost ~90 ms). */
	TArray<int32> Shown;
	TArray<FBurn> Burns;
	FRandomStream Rng;
	FVector Wind = FVector::ZeroVector;
};
