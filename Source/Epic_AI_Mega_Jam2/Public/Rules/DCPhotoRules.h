// Pure photo grading rules: no engine world, no rendering. The gameplay layer measures the 3D scene at the
// shutter moment into FDCPhotoInputs; GradePhoto turns it into a score using text tuning (Config/DefaultGame.ini).
#pragma once

#include "CoreMinimal.h"
#include "DCPhotoRules.generated.h"

/** One timing window relative to a blast's detonation (seconds after the bloom peak). */
USTRUCT(BlueprintType)
struct FDCTimingWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName Label;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float MinSeconds = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float MaxSeconds = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float Multiplier = 1.f;
};

/** A category word (DEMOLITION, BRUTALITY, DRAMA...) earned when a measured metric reaches a threshold. */
USTRUCT(BlueprintType)
struct FDCCategoryRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName Tag;
	/** Key into FDCPhotoInputs::Metrics. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName Metric;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float Threshold = 1.f;
	/** true: earned when the metric is at or below the threshold (distance-like metrics). */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") bool bAtMost = false;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") int32 BasePoints = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float PointsPerUnit = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float MaxUnits = 10.f;
};

USTRUCT(BlueprintType)
struct FDCPhotoTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Photo") TArray<FDCTimingWindow> TimingWindows;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") TArray<FDCCategoryRule> Categories;
	/** Label and multiplier when the shot is outside every timing window or has no blast in frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName NoTimingLabel = TEXT("SNAPSHOT");
	UPROPERTY(EditAnywhere, Config, Category = "Photo") int32 BasePoints = 20;
	/** Points for a perfectly centred, unoccluded subject (scaled by Centering * Visibility). */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float FramingPoints = 150.f;
	/** Danger bonus at distance 0, falling linearly to 0 at DangerFalloffMeters. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float DangerPoints = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float DangerFalloffMeters = 60.f;
	/** Multiplier applied when a spotlight unit lights the photographer (blown-out exposure). */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float SpotlitMultiplier = 0.25f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName SpotlitLabel = TEXT("BLOWN OUT");
	/** Co-star (R20): a living, unoccluded teammate in frame near the blast. Pays by how much of the frame height
	 *  they fill (from CoStarMinFill to CoStarFullFill), never by how close they are to dying. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName CoStarTag = TEXT("CO-STAR");
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float CoStarMinFill = 0.08f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float CoStarFullFill = 0.45f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float CoStarMaxMultiplier = 1.5f;
	/** The co-star must stand within this of the blast or collapse in frame (metres). */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float CoStarBlastMeters = 40.f;
	/** Share of the photo's score paid to the co-star as Propaganda Points. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float CoStarShare = 0.25f;
	/** Last Frame (R21): the downed player's one shutter from the ground. Capped at the sector's best living shot. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FName LastFrameTag = TEXT("LAST FRAME");
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float LastFrameMultiplier = 1.6f;
	/** Solo only: world slow motion while the dying player frames the shot (co-op: a local look, no world slow-mo). */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float LastFrameSlowMo = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float LastFrameSlowMoSeconds = 2.f;
	/** A wipe waits this long for the last downed player to take a Last Frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Photo") float LastFrameWipeGraceSeconds = 3.f;
};

/** What the gameplay layer measured at the shutter moment. Metric keys are named in the tuning. */
USTRUCT(BlueprintType)
struct FDCPhotoInputs
{
	GENERATED_BODY()

	/** Seconds after the detonation of the blast in frame; unset when no blast is in frame. */
	UPROPERTY() float SecondsSinceBlast = 0.f;
	UPROPERTY() bool bHasBlast = false;
	/** 0..1, 1 = subject dead centre. */
	UPROPERTY() float Centering = 0.f;
	/** 0..1, fraction of subject sample points not occluded. */
	UPROPERTY() float Visibility = 0.f;
	/** Metres from the photographer to the nearest danger (blast, collapse, enemy). */
	UPROPERTY() float DangerMeters = 1000.f;
	UPROPERTY() bool bSpotlit = false;
	/** Gear and mutator multiplier on the whole score (Cursed Disposable, Solar Eclipse...). */
	UPROPERTY() float ExtraMultiplier = 1.f;
	/** Scales the danger bonus (Fisheye Danger Lens). */
	UPROPERTY() float DangerScale = 1.f;
	/** Last Frame (R21): taken from the ground while downed; its score never beats LastFrameCap (the sector's best
	 *  living shot; with none yet, the raw points before multipliers). */
	UPROPERTY() bool bLastFrame = false;
	UPROPERTY() int32 LastFrameCap = 0;
	/** Co-star (R20): fraction of the frame the best qualifying teammate fills (0 = none). */
	UPROPERTY() float CoStarFill = 0.f;
	/** Who that teammate is (for the credit and their share); empty when none. */
	UPROPERTY() FString CoStarName;
	UPROPERTY() int32 CoStarPlayerId = -1;
	/** Named counts for category rules: Collapses, Debris, Enemies, Blasts, Walkers... */
	UPROPERTY() TMap<FName, float> Metrics;
};

USTRUCT(BlueprintType)
struct FDCPhotoGrade
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Photo") int32 Score = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") int32 RawPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") float Multiplier = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") FName TimingLabel;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") TArray<FName> Categories;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") bool bSpotlit = false;
	UPROPERTY(BlueprintReadOnly, Category = "Photo") bool bLastFrame = false;
	/** Co-star multiplier applied (1 = none). */
	UPROPERTY(BlueprintReadOnly, Category = "Photo") float CoStarMultiplier = 1.f;
};

namespace DCPhotoRules
{
	/** The timing window the shot falls into, or nullptr. Windows are checked in declared order. */
	EPIC_AI_MEGA_JAM2_API const FDCTimingWindow* FindWindow(const FDCPhotoTuning& Tuning, float SecondsSinceBlast);

	/** Pure: same inputs, same grade, on host and client. */
	EPIC_AI_MEGA_JAM2_API FDCPhotoGrade GradePhoto(const FDCPhotoInputs& In, const FDCPhotoTuning& Tuning);
	/** Co-star multiplier for a frame fill (1 below the minimum, rising linearly to the maximum at full fill). */
	EPIC_AI_MEGA_JAM2_API float CoStarMultiplier(float Fill, const FDCPhotoTuning& Tuning);
}
