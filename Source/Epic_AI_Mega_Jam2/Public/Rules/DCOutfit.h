// Photographer outfits (R23, R18): a character is a small recipe (one part per slot, two palette swatches and a glow) that the
// share code carries and the host replicates; every machine builds the same voxels from it. Pure: no engine objects.
#pragma once

#include "CoreMinimal.h"
#include "DCOutfit.generated.h"

/** The voxel photographer's own body (advisor CP14, review R10/R23): one rigid voxel segment per bone of the
 *  mannequin skeleton, which stays as the hidden animation driver. With an Aim bone the segment runs from Bone to Aim
 *  in the reference pose (model X along the bone, stretched to the bone length plus OverlapCm so joints close; model
 *  Z toward the actor's front), so mirrored right-side bone axes need no data; without one it sits at Offset and
 *  Rotation in actor space like an outfit part. Colours use the outfit swatches (SwatchMain / SwatchAccent). */
USTRUCT(BlueprintType)
struct FDCBodySegment
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Bone;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Aim;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Model;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FVector Offset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") float OverlapCm = 4.f;
};

/** One voxel part: a voxel-forge model worn on a mannequin bone (data rows in Config/DefaultGame.ini). */
USTRUCT(BlueprintType)
struct FDCOutfitPart
{
	GENERATED_BODY()
	/** Head, Body, Gear or Camera (the slots, in the order Outfit.Slots lists them). */
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Slot;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FText DisplayName;
	/** Model name (Content/DC/Voxel/<Model>.dcvox); none = nothing worn in this slot. */
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Model;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Socket;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FVector Offset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") float Scale = 1.f;
	/** A mannequin bone the part covers (hidden while worn), e.g. the head under a voxel head. */
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName HideBone;
};

/** A colour the player may pick: a palette material by name (the art bible palette, never a free colour). */
USTRUCT(BlueprintType)
struct FDCOutfitSwatch
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Material;
};

/** A whole look by ids (R18): one part id per slot (in Outfit slot order), swatch ids for main and accent, a glow id. */
USTRUCT(BlueprintType)
struct FDCOutfitPreset
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FText DisplayName;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") TArray<FName> Parts;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Main;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Accent;
	UPROPERTY(EditAnywhere, Config, Category = "Outfit") FName Glow;
};

/** The recipe: per slot an index into that slot's parts, two swatch indices (main, accent) and a glow index (R18). */
USTRUCT(BlueprintType)
struct FDCOutfit
{
	GENERATED_BODY()
	UPROPERTY() TArray<uint8> Parts;
	UPROPERTY() uint8 Main = 0;
	UPROPERTY() uint8 Accent = 1;
	/** Index into OutfitGlows (the SwatchGlow colour). DC1 codes predate it and load as 0. */
	UPROPERTY() uint8 Glow = 0;

	bool operator==(const FDCOutfit& O) const { return Parts == O.Parts && Main == O.Main && Accent == O.Accent && Glow == O.Glow; }
};

namespace DCOutfit
{
	/** Parts of one slot, in data order (the index a recipe stores). */
	EPIC_AI_MEGA_JAM2_API TArray<const FDCOutfitPart*> PartsOf(FName Slot, const TArray<FDCOutfitPart>& Parts);

	/** Share code in the newest version: "DC2-" + base32 of the bytes + a check character, e.g. DC2-0482A-K. */
	EPIC_AI_MEGA_JAM2_API FString Encode(const FDCOutfit& Outfit);
	/** Untrusted text, any known version (DC1 = main and accent, DC2 adds the glow): false with a reason on an unknown
	 *  prefix, bad character, bad check, or an index past the data. Out is left alone on failure. */
	EPIC_AI_MEGA_JAM2_API bool Decode(const FString& Code, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, int32 GlowCount, FDCOutfit& Out, FString& Error);
	/** The first part of every slot, swatches 0 and 2, glow 0. */
	EPIC_AI_MEGA_JAM2_API FDCOutfit Default(const TArray<FName>& Slots);
	/** Next or previous part (wraps) in a slot. */
	EPIC_AI_MEGA_JAM2_API void Cycle(FDCOutfit& Outfit, int32 SlotIndex, int32 Step, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts);
	/** Steps an index through Count entries, wrapping both ways (swatches, glows, presets). */
	EPIC_AI_MEGA_JAM2_API uint8 Wrap(int32 Index, int32 Step, int32 Count);
	/** A preset resolved by ids into an outfit; false with a reason when an id is not in the data. */
	EPIC_AI_MEGA_JAM2_API bool FromPreset(const FDCOutfitPreset& Preset, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, const TArray<FDCOutfitSwatch>& Swatches, const TArray<FDCOutfitSwatch>& Glows, FDCOutfit& Out, FString& Error);
	/** A random outfit from the stream: any part per slot, main and accent differ when there are two swatches. */
	EPIC_AI_MEGA_JAM2_API FDCOutfit Random(FRandomStream& Rng, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, int32 GlowCount);
}
