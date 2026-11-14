// Photographer outfits (R23): a character is a small recipe (one part per slot plus two palette swatches) that the
// share code carries and the host replicates; every machine builds the same voxels from it. Pure: no engine objects.
#pragma once

#include "CoreMinimal.h"
#include "DCOutfit.generated.h"

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

/** The recipe: per slot an index into that slot's parts, and two swatch indices (main, accent). */
USTRUCT(BlueprintType)
struct FDCOutfit
{
	GENERATED_BODY()
	UPROPERTY() TArray<uint8> Parts;
	UPROPERTY() uint8 Main = 0;
	UPROPERTY() uint8 Accent = 1;

	bool operator==(const FDCOutfit& O) const { return Parts == O.Parts && Main == O.Main && Accent == O.Accent; }
};

namespace DCOutfit
{
	/** Parts of one slot, in data order (the index a recipe stores). */
	EPIC_AI_MEGA_JAM2_API TArray<const FDCOutfitPart*> PartsOf(FName Slot, const TArray<FDCOutfitPart>& Parts);

	/** Share code: "DC1-" + base32 of the bytes + a check character, e.g. DC1-0482A-K. */
	EPIC_AI_MEGA_JAM2_API FString Encode(const FDCOutfit& Outfit);
	/** Untrusted text: false with a reason on a bad prefix, bad character, bad check, or an index past the data. */
	EPIC_AI_MEGA_JAM2_API bool Decode(const FString& Code, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, FDCOutfit& Out, FString& Error);
	/** The first part of every slot, swatches 0 and 1. */
	EPIC_AI_MEGA_JAM2_API FDCOutfit Default(const TArray<FName>& Slots);
	/** Next or previous part (wraps) in a slot; next swatch (wraps). */
	EPIC_AI_MEGA_JAM2_API void Cycle(FDCOutfit& Outfit, int32 SlotIndex, int32 Step, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts);
}
