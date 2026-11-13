// Canvas HUD. One draw function per run phase, looked up in a table. Layout numbers live here; wording and
// colours that a designer tunes live in UDCTuning.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DCTypes.h"
#include "DCHUD.generated.h"

class ADCCharacter;
class ADCPlayerController;
class ADCGameState;
class UFont;
class UTexture;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** Draws a graded photo with its stamp (used by the stamp, the reel and the front page). */
	void DrawPhotoCard(UTexture* Image, const FDCPhotoGrade& Grade, float X, float Y, float W, float StampSlam, bool bShowStamp);

	// Shared drawing helpers.
	void Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale = 1.f, bool bCentre = false, UFont* Font = nullptr);
	/** Size of S drawn by Text at PixelScale (= Scale * TextBoost * UiScale). */
	void TextSize(UFont* Font, const FString& S, float PixelScale, float& OutW, float& OutH) const;
	void Box(float X, float Y, float W, float H, const FLinearColor& C);
	void Frame(float X, float Y, float W, float H, float T, const FLinearColor& C);
	float Sx(float X) const { return X * UiScale; }
	/** Draws a clickable button (a HUD hit box named Id); returns bEnabled. */
	bool Button(const FString& Label, FName Id, float X, float Y, float W, float H, bool bEnabled, bool bSelected = false);
	virtual void NotifyHitBoxClick(FName BoxName) override;

	/** The engine large font draws at about 16 px at 1080p; layout scales are written against twice that. */
	static constexpr float TextBoost = 2.f;

private:
	void DrawSector(ADCGameState& GS, ADCCharacter* Me);
	void DrawLobby(ADCGameState& GS, ADCCharacter* Me);
	void DrawDraft(ADCGameState& GS, ADCCharacter* Me);
	void DrawReel(ADCGameState& GS, ADCCharacter* Me);
	void DrawFinalCut(ADCGameState& GS, ADCCharacter* Me);
	void DrawVote(ADCGameState& GS, ADCCharacter* Me);
	void DrawFrontPage(ADCGameState& GS, ADCCharacter* Me);
	void DrawScoop(ADCGameState& GS);
	void DrawCrosshair(ADCCharacter& Me);
	void DrawViewfinder(ADCCharacter& Me);
	void DrawStrikeInfo(ADCCharacter& Me);
	void DrawStatus(ADCGameState& GS, ADCCharacter& Me);
	void DrawStamp(ADCPlayerController& PC);
	void DrawHurt(ADCCharacter& Me);
	void DrawOnboarding(ADCCharacter& Me);
	void DrawFlashes(ADCGameState& GS, ADCCharacter& Me);
	void DrawToast(ADCCharacter& Me);
	void DrawDowned(ADCCharacter& Me);
	void TrackOnboarding(ADCCharacter& Me);

	float UiScale = 1.f;
	uint8 ReelStageSeen = 255;
	int32 ReelIndexSeen = -1;
	double ReelStageReal = 0.0;
	bool bPressPlayed = false;
	double RealNow = 0.0;

	// First-minute onboarding: which verbs this player has done.
	bool bDidMove = false, bDidShoot = false, bDidStrike = false, bDidRaise = false, bDidSnap = false;
	FVector SpawnLocation = FVector::ZeroVector;
	bool bHaveSpawn = false;
	double OnboardDoneReal = -1.0;

	/** Ink grit for stamps: a code-made mask of speckles and worn edges, drawn in paper over the stamp ink (R5). */
	class UTexture2D* StampGrit();
	UPROPERTY() TObjectPtr<class UTexture2D> Grit;
	UPROPERTY() TObjectPtr<UFont> BigFont;
	UPROPERTY() TObjectPtr<UFont> HeadFont;
	UPROPERTY() TObjectPtr<UFont> MastFont;
	UPROPERTY() TObjectPtr<UFont> StampFont;
	UPROPERTY() TObjectPtr<UFont> SmallFont;
};
