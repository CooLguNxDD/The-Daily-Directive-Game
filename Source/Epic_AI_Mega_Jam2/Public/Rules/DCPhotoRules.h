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
};

namespace DCPhotoRules
{
	/** The timing window the shot falls into, or nullptr. Windows are checked in declared order. */
	EPIC_AI_MEGA_JAM2_API const FDCTimingWindow* FindWindow(const FDCPhotoTuning& Tuning, float SecondsSinceBlast);

	/** Pure: same inputs, same grade, on host and client. */
	EPIC_AI_MEGA_JAM2_API FDCPhotoGrade GradePhoto(const FDCPhotoInputs& In, const FDCPhotoTuning& Tuning);
}
