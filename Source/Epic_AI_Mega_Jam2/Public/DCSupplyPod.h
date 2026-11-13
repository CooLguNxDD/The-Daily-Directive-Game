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
	bool HasLanded() const { return bLanded; }

private:
	UPROPERTY(Replicated) float LandTime = 0.f;
	UPROPERTY(Replicated) FVector_NetQuantize Ground;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Stripe;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	bool bLanded = false;
};
