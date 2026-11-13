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

	// Draft (R10).
	UPROPERTY(Replicated) TArray<FName> DraftOffer;
	UPROPERTY(Replicated) TArray<FName> Upgrades;
	UPROPERTY(Replicated) bool bDraftDone = false;
	/** Camera Gear effects owned (Strobe, Fisheye...). */
	UPROPERTY(Replicated) TArray<FName> Gear;
	/** Multiplies every photo score (Cursed Disposable). */
	UPROPERTY(Replicated) float PhotoMultiplier = 1.f;
	/** Multiplies the danger bonus (Fisheye Danger Lens). */
	UPROPERTY(Replicated) float DangerMultiplier = 1.f;
	/** Viewfinder field of view added by gear (degrees). */
	UPROPERTY(Replicated) float ExtraFOV = 0.f;

	// Final cut and vote (R14, R15).
	/** Best shots this player brings to the final cut, critic order (photo ids). */
	UPROPERTY(Replicated) TArray<int32> FinalCutChoices;
	UPROPERTY(Replicated) TArray<int32> FinalCut;
	UPROPERTY(Replicated) bool bFinalCutDone = false;
	UPROPERTY(Replicated) int32 VotedFor = 0;

	bool HasGear(FName Id) const { return Gear.Contains(Id); }

	/** Host: the press call sign every screen shows (R8). The engine's player name (the PC's network name on LAN
	 *  or online subsystems) is never shown: GetPlayerName() returns this through GetPlayerNameCustom. */
	void SetCallSign(const FString& InSign) { CallSign = InSign; ForceNetUpdate(); }
	const FString& GetCallSign() const { return CallSign; }
	virtual FString GetPlayerNameCustom() const override;

private:
	UPROPERTY(Replicated) FString CallSign;
};
