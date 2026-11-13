#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DCTypes.h"
#include "DCGameState.generated.h"

class ADCEnvironment;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ADCGameState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Host only: records a detonation every machine can read for scoring and tags. */
	int32 AddBlast(const FVector& Location, float Radius, FName StratagemId);

	const FDCBlastRecord* FindBlast(int32 Id) const;

	/** Synced clock: host time on every machine. */
	float Now() const { return static_cast<float>(GetServerWorldTimeSeconds()); }

	ADCEnvironment* GetEnvironment() const { return Environment; }

	UPROPERTY(Replicated) TArray<FDCBlastRecord> Blasts;
	UPROPERTY(Replicated) EDCRunPhase Phase = EDCRunPhase::Sector;
	UPROPERTY(Replicated) int32 SectorIndex = 0;
	UPROPERTY(Replicated) float SectorEndTime = 0.f;
	UPROPERTY(Replicated) int32 RunSeed = 0;

private:
	UPROPERTY() TObjectPtr<ADCEnvironment> Environment;
	int32 NextBlastId = 1;
};
