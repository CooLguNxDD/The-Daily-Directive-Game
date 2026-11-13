// A destructible tower built from a modular block kit. The host spawns it from a seeded spec and decides when it
// collapses; each machine rebuilds the same blocks from the replicated spec and simulates its own debris.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/DCCityPlan.h"
#include "DCBuilding.generated.h"

class UInstancedStaticMeshComponent;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCBuilding : public AActor
{
	GENERATED_BODY()

public:
	ADCBuilding();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Host: set before FinishSpawning. */
	void InitSpec(const FDCBuildingSpec& InSpec) { Spec = InSpec; }

	/** Host only: collapse now, pushed away from Epicenter. Idempotent. */
	void Collapse(const FVector& Epicenter);

	bool IsCollapsed() const { return bCollapsed; }
	float GetCollapseTime() const { return CollapseTime; }
	const FDCBuildingSpec& GetSpec() const { return Spec; }

	/** World-space centre of the standing volume (or the rubble after collapse). */
	FVector GetCenter() const;
	float GetHeight() const { return Spec.Floors * Spec.BlockSize; }
	/** Points spread over the building for occlusion / framing checks. */
	void GetSamplePoints(TArray<FVector>& Out) const;

private:
	UFUNCTION() void OnRep_Spec();
	UFUNCTION() void OnRep_Collapsed();
	void BuildBlocks();
	void SimulateCollapse();

	UPROPERTY(ReplicatedUsing = OnRep_Spec) FDCBuildingSpec Spec;
	UPROPERTY(ReplicatedUsing = OnRep_Collapsed) bool bCollapsed = false;
	UPROPERTY(Replicated) float CollapseTime = 0.f;
	UPROPERTY(Replicated) FVector_NetQuantize Epicenter;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Walls;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Windows;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Trim;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Rubble;

	bool bBuilt = false;
	bool bSimulated = false;
};
