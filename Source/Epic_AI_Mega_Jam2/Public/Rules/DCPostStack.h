// Modular post-process stack (R19), pure: layers are data rows; code only sets each layer's weight by id (the blast
// flash, the viewfinder, damage, weather, the photo). Compose walks the layers in priority order: additive fields add
// Weight * value, multiplicative fields scale by lerp(1, value, Weight). Same layers, same weights, same look.
#pragma once

#include "CoreMinimal.h"
#include "DCPostStack.generated.h"

USTRUCT(BlueprintType)
struct FDCPostLayer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Post") FName Id;
	/** Lower first. */
	UPROPERTY(EditAnywhere, Config, Category = "Post") int32 Priority = 0;
	/** Weight when no code sets it (the base grade and the sector look are always on). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") float DefaultWeight = 0.f;
	/** Skipped below this scalability level (0 low, 1 medium, 2 high). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") int32 MinQuality = 0;
	// Additive
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Bloom = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Exposure = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Vignette = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Grain = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Outline = 0.f;
	/** Bloom threshold in scene luminance (additive): only light above it blooms. The composed value <= 0 turns the
	 *  threshold off (everything blooms, the engine's physically based default). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") float BloomThreshold = 0.f;
	// Multiplicative
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Saturation = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float Contrast = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") FLinearColor Gain = FLinearColor::White;
	/** While this layer is on, the composed bloom and exposure never exceed these (0 = no cap): the photo keeps a
	 *  readable peak however hot the blast (min-op, applied after every layer). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") float BloomCap = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float ExposureCap = 0.f;
	/** The player's screen only (viewfinder, damage, dying look, flash kick): never in a photo or reel capture. */
	UPROPERTY(EditAnywhere, Config, Category = "Post") bool bScreenOnly = false;
};

/** A scalability preset: the quality level layers check, plus engine settings applied once. */
USTRUCT(BlueprintType)
struct FDCPostPreset
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Post") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Post") int32 Quality = 2;
	UPROPERTY(EditAnywhere, Config, Category = "Post") float ScreenPercentage = 100.f;
	/** Console variables set when the preset applies ("r.Name=Value"). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") TArray<FString> ConsoleVariables;
	/** Local cosmetic load (advisor a-8): pooled prop lights, crumbs and faction cue lights scale by this. Never
	 *  anything the host decides (fragment trees, rubble), so co-op hashes do not depend on it. */
	UPROPERTY(EditAnywhere, Config, Category = "Post") float EffectScale = 1.f;
};

/** The composed look. Neutral by default. */
USTRUCT(BlueprintType)
struct FDCPostResult
{
	GENERATED_BODY()
	UPROPERTY() float Bloom = 0.f;
	UPROPERTY() float Exposure = 0.f;
	UPROPERTY() float Vignette = 0.f;
	UPROPERTY() float Grain = 0.f;
	UPROPERTY() float Outline = 0.f;
	UPROPERTY() float BloomThreshold = 0.f;
	UPROPERTY() float Saturation = 1.f;
	UPROPERTY() float Contrast = 1.f;
	UPROPERTY() FLinearColor Gain = FLinearColor::White;
};

namespace DCPostStack
{
	/** Composes every layer whose weight (set, else default) is above zero and whose MinQuality <= Quality. */
	EPIC_AI_MEGA_JAM2_API FDCPostResult Compose(const TArray<FDCPostLayer>& Layers, const TMap<FName, float>& Weights, int32 Quality);
}
