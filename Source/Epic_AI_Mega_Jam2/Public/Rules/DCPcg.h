// PCG-style randomiser (R15), pure: rule sets are data rows (weights, numeric ranges, adjacency, symmetry, palette
// slots); a seed and a cell index pick values. Same seed, same picks on every machine. The city builder (districts,
// lot kinds, props, recipe palettes) rolls everything through here.
#pragma once

#include "CoreMinimal.h"
#include "DCPcg.generated.h"

USTRUCT(BlueprintType)
struct FDCPcgChoice
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") FName Value;
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") float Weight = 1.f;
};

/** One slot of a rule set: a weighted choice, a numeric range, or both. */
USTRUCT(BlueprintType)
struct FDCPcgRule
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") FName Slot;
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") TArray<FDCPcgChoice> Choices;
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") float Min = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") float Max = 0.f;
	/** Adjacency: these values never sit next to an equal value in a grid roll (e.g. no two landmarks side by side). */
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") TArray<FName> NoSameNeighbour;
	/** Symmetry for grid rolls: None, X (mirror across the vertical axis) or Y. */
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") FName Mirror;
};

USTRUCT(BlueprintType)
struct FDCPcgRuleSet
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Pcg") TArray<FDCPcgRule> Rules;

	const FDCPcgRule* Find(FName Slot) const { return Rules.FindByPredicate([Slot](const FDCPcgRule& R) { return R.Slot == Slot; }); }
};

namespace DCPcg
{
	/** Order-independent hash of (seed, salt, index): every pick is a pure function of its inputs. */
	EPIC_AI_MEGA_JAM2_API uint32 Hash(int32 Seed, uint32 Salt, int32 Index);
	/** 0..1 from the hash. */
	EPIC_AI_MEGA_JAM2_API float Unit(int32 Seed, uint32 Salt, int32 Index);

	/** Weighted choice of a slot for one index (None when the slot has no choices). */
	EPIC_AI_MEGA_JAM2_API FName Pick(const FDCPcgRule& Rule, int32 Seed, int32 Index);
	/** Number in [Min, Max] for one index. */
	EPIC_AI_MEGA_JAM2_API float Range(const FDCPcgRule& Rule, int32 Seed, int32 Index);
	EPIC_AI_MEGA_JAM2_API int32 RangeInt(const FDCPcgRule& Rule, int32 Seed, int32 Index);

	/** Fills a W x H grid with picks of a slot, honouring Mirror and NoSameNeighbour (a conflicting cell re-rolls with
	 *  the next salt, up to a few times, then takes the first choice that does not conflict). Row-major, x fastest. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> PickGrid(const FDCPcgRule& Rule, int32 Seed, int32 W, int32 H);
}
