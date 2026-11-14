// Newsprint photo treatment (R5), pure: levels so the frame reads even at a blast peak, then an ink halftone over
// paper with grain and a printed border. Runs on the photo's pixels at the shutter (CPU), so every screen and the
// upload show the same print. Tuning as data (UDCTuning Camera.Print).
#pragma once

#include "CoreMinimal.h"
#include "DCNewsprint.generated.h"

USTRUCT(BlueprintType)
struct FDCPrintTuning
{
	GENERATED_BODY()
	/** Levels: map these luminance percentiles to black and white, gain capped (exposure kept for photos). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float LowPercentile = 0.02f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float HighPercentile = 0.97f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MaxGain = 3.f;
	/** The black point never rises above this luma (0..255): a dark subject is lifted, not crushed to ink. */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MaxBlackPoint = 18.f;
	/** Mid-tone lift: the frame's median luminance is pulled toward this (0..1) by a gamma no lower than MinGamma, so a
	 *  tower beside a white fireball still reads. */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float TargetMid = 0.42f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MinGamma = 0.35f;
	/** Halftone cell edge in pixels (0 = no halftone). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") int32 DotCell = 4;
	/** How much of the halftone print replaces the photo (0 = photo only, 1 = pure ink dots). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float HalftoneMix = 0.55f;
	/** How much of the photo's own colour survives under the paper tone (blast orange stays on blasts). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float ColorKeep = 0.45f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") FColor Ink = FColor(22, 22, 26);
	UPROPERTY(EditAnywhere, Config, Category = "Print") FColor Paper = FColor(242, 232, 208);
	/** Grain amplitude in 0..255 levels. */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float Grain = 14.f;
	/** Paper border, then a thin ink rule inside it (pixels). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") int32 BorderPx = 10;
	UPROPERTY(EditAnywhere, Config, Category = "Print") int32 RulePx = 2;
	/** News-feed treatment (R25): every Nth row darkened by ScanlineDark (0 = no scanlines). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") int32 ScanlineEvery = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float ScanlineDark = 0.f;
	/** News-feed chromatic split (R25): red sampled this many pixels to the left, blue to the right (0 = none). */
	UPROPERTY(EditAnywhere, Config, Category = "Print") int32 ChromaSplitPx = 0;
	/** Photo meter (advisor CP21, critic V20.4/V25.2): the float capture is developed before printing. Gain brings the
	 *  centre-weighted log-average luminance (linear) to MeterKey, between 2^-MeterMinStops and 2^MeterMaxStops; a
	 *  soft shoulder at MeterWhite (linear) keeps a blast core distinct instead of clipping it flat. 0 key = off. */
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MeterKey = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MeterMaxStops = 2.f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MeterMinStops = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MeterCenterWeight = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Print") float MeterWhite = 4.f;
	/** Photo source (advisor CP28): true = the capture is SCS_FinalColorHDR (post-exposure, post-grade scene-linear,
	 *  no tone curve), developed without a decode or a clamp so highlights keep their range up to MeterWhite; false =
	 *  SCS_FinalColorLDR, decoded from display gamma first. */
	UPROPERTY(EditAnywhere, Config, Category = "Print") bool bSceneLinear = false;
};

namespace DCNewsprint
{
	/** Levels: returns the gain and offset that map the percentiles to 0..255 (gain clamped to MaxGain). */
	EPIC_AI_MEGA_JAM2_API void Levels(const TArray<FColor>& Pixels, const FDCPrintTuning& T, float& OutGain, float& OutOffset);
	/** Gamma that brings the median (after levels) toward TargetMid; 1 when the frame is already bright enough. */
	EPIC_AI_MEGA_JAM2_API float MidGamma(const TArray<FColor>& Pixels, const FDCPrintTuning& T, float Gain, float Offset);

	/** Meter gain for a display-encoded float capture (values 0..1, sRGB-like): see FDCPrintTuning Meter*. */
	EPIC_AI_MEGA_JAM2_API float MeterGain(const TArray<FLinearColor>& Pixels, int32 Width, int32 Height, const FDCPrintTuning& T);
	/** Develops a float capture into 8-bit pixels: decode, metered gain, soft shoulder, encode (no 8-bit stretch). */
	EPIC_AI_MEGA_JAM2_API void Develop(const TArray<FLinearColor>& In, int32 Width, int32 Height, const FDCPrintTuning& T, TArray<FColor>& Out);

	/** Prints the photo in place: levels, paper tone, halftone, grain (seeded), border. */
	EPIC_AI_MEGA_JAM2_API void Print(TArray<FColor>& Pixels, int32 Width, int32 Height, const FDCPrintTuning& T, uint32 Seed);
}
