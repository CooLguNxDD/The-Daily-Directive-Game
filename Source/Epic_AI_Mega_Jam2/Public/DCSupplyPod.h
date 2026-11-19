// HQ Scoop reward (R8): a supply pod drops from orbit beside the photographer. It lands after a short fall (server
// time); the first living player who walks into it gets film, ammo and health. The host decides; visuals are local.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCSupplyPod.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCSupplyPod : public AActor
{
	GENERATED_BODY()

public:
	ADCSupplyPod();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static ADCSupplyPod* Drop(UWorld* World, const FVector& Where);
	/** R7 supply drop: a pod with its own load that refills every standing player within CrewRadiusCm of it once the
	 *  first one reaches it (host). FallDelay = seconds before it starts falling (the call's telegraph). */
	static ADCSupplyPod* DropLoad(UWorld* World, const FVector& Where, int32 Film, int32 Ammo, float Health, float CrewRadiusCm, float FallDelay);
	bool HasLanded() const { return bLanded; }

private:
	UPROPERTY(Replicated) float LandTime = 0.f;
	UPROPERTY(Replicated) FVector_NetQuantize Ground;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Stripe;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	bool bLanded = false;
	/** Host-only load (negative = the Run pod defaults; CrewRadiusCm 0 = only the player who reaches it). */
	int32 LoadFilm = -1, LoadAmmo = -1;
	float LoadHealth = -1.f, LoadCrewRadiusCm = 0.f;
};
