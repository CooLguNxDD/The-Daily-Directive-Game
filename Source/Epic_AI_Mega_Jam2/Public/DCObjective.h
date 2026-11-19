// The sector's objective (R4) as one replicated actor: the host writes its state (DCObjectiveDirector in the game
// mode), every machine draws it from that state: the package (on the street or on its carrier's back), signal-yellow
// marker pillars over the towers to bring down, and the extraction beacon. Lime-yellow because the crew uses them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/DCObjectiveRules.h"
#include "DCObjective.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UProceduralMeshComponent;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCObjective : public AActor
{
	GENERATED_BODY()

public:
	ADCObjective();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	const FDCObjectiveDef* GetDef() const;
	static ADCObjective* Find(const UObject* WorldContext);

	UPROPERTY(Replicated) FName DefId;
	UPROPERTY(ReplicatedUsing = OnRep_Stage) EDCObjectiveStage Stage = EDCObjectiveStage::None;
	UFUNCTION() void OnRep_Stage();
	UPROPERTY(Replicated) FVector_NetQuantize PackageAt;
	/** Who carries the package (Retrieve); null while it lies on the street. */
	UPROPERTY(Replicated) TObjectPtr<APlayerState> Carrier;
	UPROPERTY(Replicated) TArray<FVector_NetQuantize> TargetAt;
	UPROPERTY(Replicated) TArray<uint8> TargetDown;
	UPROPERTY(Replicated) FVector_NetQuantize ExtractAt;

	int32 TargetsLeft() const;
	/** Where the HUD points: the package, the nearest standing target, or the beacon (false when nothing to point at). */
	bool Waypoint(const FVector& From, FVector& Out) const;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beacon;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> BeaconLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> PackageLight;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Package;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Markers;
	bool bBuilt = false;
	EDCObjectiveStage ShownStage = EDCObjectiveStage::None;
	void BuildVisuals();
};
