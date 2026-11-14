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
	/** Reel re-staging: a local, effects-only copy (no damage, no records, no hitstop). Set before FinishSpawning. */
	void InitRestage(FName InStratagemId, float InDetonateTime) { StratagemId = InStratagemId; DetonateTime = InDetonateTime; bRestage = true; }

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
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beacon;
	/** Where the beam meets the street: a pulsing white-orange flare disc (V4.5), gone at detonation. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ImpactFlare;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FlareMat;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Fireball;
	/** Voxel blast core (R4): seeded chunky cubes with gaps between them, so a tower stays readable behind the peak. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> FireCubes;
	/** Per cube: direction * reach (0..1 of the fireball) in XYZ, size (0..1 of the fireball) in W. */
	TArray<FVector4f> CubeSeeds;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Shock;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> FillLight;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BeamMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FireMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShockMat;
	UPROPERTY() TObjectPtr<AController> InstigatorController;

	/** Host behaviour during the telegraph, by the stratagem's Behaviour id (Singularity pulls, Decoy lures). */
	void TickBehaviour(const FDCStratagemDef& Def, float U, float DeltaSeconds);

	bool bRestage = false;
	bool bLocalDetonated = false;
	/** Host: the strike's voxel op is appended (PlanLeadSeconds before detonation, so it is planned by then). */
	bool bOpQueued = false;
	void QueueVoxelOp(const struct FDCStratagemDef& Def);
	bool bChargePlayed = false;
	float NextBeepTime = 0.f;
};
