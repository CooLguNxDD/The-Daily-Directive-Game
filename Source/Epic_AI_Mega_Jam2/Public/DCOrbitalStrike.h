// One orbital blast: telegraph (beacon + converging laser + rising beeps), then detonation at a host-chosen
// server time. Every machine plays the telegraph and the bloom peak from the replicated detonation time; only the
// host applies damage, records the blast and collapses buildings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCOrbitalStrike.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
struct FDCStratagemDef;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCOrbitalStrike : public AActor
{
	GENERATED_BODY()

public:
	ADCOrbitalStrike();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Host: set before FinishSpawning. */
	void Init(FName InStratagemId, float InDetonateTime, AController* InInstigator);

	float GetDetonateTime() const { return DetonateTime; }
	bool HasDetonated() const { return bLocalDetonated; }
	FName GetStratagemId() const { return StratagemId; }
	const FDCStratagemDef* GetDef() const;

private:
	void TickTelegraph(float Now, const FDCStratagemDef& Def);
	void TickBlast(float Now, const FDCStratagemDef& Def);
	void DetonateLocal(const FDCStratagemDef& Def);
	void DetonateHost(const FDCStratagemDef& Def);

	UPROPERTY(Replicated) FName StratagemId;
	UPROPERTY(Replicated) float DetonateTime = 0.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beacon;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Fireball;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Shock;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BeamMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FireMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShockMat;
	UPROPERTY() TObjectPtr<AController> InstigatorController;

	bool bLocalDetonated = false;
	bool bChargePlayed = false;
	float NextBeepTime = 0.f;
};
