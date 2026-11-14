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
	void DrawPhotoCard(UTexture* Image, const FDCPhotoGrade& Grade, float X, float Y, float W, float StampSlam, bool bShowStamp, const FString& Feed = FString());
	/** The news-feed line burned under a print (R25): feed id and the shot's clock time. */
	static FString FeedStamp(int32 PhotoId, float ShotTime);

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
	void DrawObjective(ADCGameState& GS, ADCCharacter& Me);
	void DrawTerritory(ADCGameState& GS, ADCCharacter& Me);
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
	bool bDidObjective = false, bDidExtract = false, bDidClash = false;
	/** First-sighting callouts (R3): factions this machine has already named, and the callout on screen. */
	TSet<FName> SeenFactions;
	FString CalloutText;
	FLinearColor CalloutColor = FLinearColor::White;
	double CalloutUntilReal = -1.0;
	void TrackFactionSightings(ADCCharacter& Me);
	void DrawCallout();
	FVector SpawnLocation = FVector::ZeroVector;
	bool bHaveSpawn = false;
	double OnboardDoneReal = -1.0;

	/** Ink grit for stamps: a code-made mask of speckles and worn edges, drawn in paper over the stamp ink (R5). */
	class UTexture2D* StampGrit();
	/** Sector title card (CP6): paper and ink over the street while the sector's voxel city is still loading, then it
	 *  slides away; the craft hitch and the empty street never show. */
	void DrawSectorCard(ADCGameState& GS);
	int32 CardSector = INDEX_NONE;
	double CardLoadedReal = -1.0;
	/** A newsprint sheet: the generated paper texture tiled at its own scale (R11), else flat paper. */
	void PaperSheet(float X, float Y, float W, float H, float Alpha = 1.f);
	UPROPERTY() TObjectPtr<class UTexture2D> PaperTex;
	bool bPaperTried = false;
	UPROPERTY() TObjectPtr<class UTexture2D> Grit;
	UPROPERTY() TObjectPtr<UFont> BigFont;
	/** Draft card art by upgrade id, loaded once at BeginPlay (never in a draw). */
	UPROPERTY() TMap<FName, TObjectPtr<class UTexture2D>> CardIcons;
	UPROPERTY() TObjectPtr<UFont> HeadFont;
	UPROPERTY() TObjectPtr<UFont> MastFont;
	UPROPERTY() TObjectPtr<UFont> StampFont;
	UPROPERTY() TObjectPtr<UFont> SmallFont;
};
