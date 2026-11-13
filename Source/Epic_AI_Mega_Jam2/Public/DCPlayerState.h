#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "DCTypes.h"
#include "DCPlayerState.generated.h"

/** Host-owned per-player economy: Propaganda Points, film, graded photos, owned stratagems. */
UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ADCPlayerState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Host: the new photo's points go straight into the wallet. */
	void AddPhoto(const FDCPhotoRecord& Record);
	void AddPoints(int32 Delta) { Points = FMath::Max(0, Points + Delta); }
	bool TrySpend(int32 Cost);
	void ResetFilm(int32 Shots) { ShotsLeft = Shots; }
	bool ConsumeShot();

	bool OwnsStratagem(FName Id) const { return OwnedStratagems.Contains(Id); }
	float CooldownReadyTime(FName Id) const;
	void SetCooldown(FName Id, float ReadyTime);

	const FDCPhotoRecord* FindPhoto(int32 PhotoId) const;

	UPROPERTY(Replicated) int32 Points = 0;
	UPROPERTY(Replicated) int32 ShotsLeft = 0;
	UPROPERTY(Replicated) int32 FilmPerSector = 15;
	UPROPERTY(Replicated) TArray<FDCPhotoRecord> Photos;
	UPROPERTY(Replicated) TArray<FName> OwnedStratagems;
	UPROPERTY(Replicated) TArray<FName> CooldownIds;
	UPROPERTY(Replicated) TArray<float> CooldownTimes;
	UPROPERTY(Replicated) int32 Kills = 0;
};
