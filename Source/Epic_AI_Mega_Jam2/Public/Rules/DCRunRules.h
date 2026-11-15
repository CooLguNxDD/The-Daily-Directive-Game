// Pure run rules: draft offers, HQ Scoop checks, the critic's sort, page scores and headlines. No world, no
// rendering; the game mode feeds them data and applies the results. Tested under -nullrhi.
#pragma once

#include "CoreMinimal.h"
#include "DCTypes.h"
#include "DCRunRules.generated.h"

struct FDCUpgradeDef;
struct FDCScoopDef;

/** A draft card rarity (R21, human 2026-11-15): how often its cards are dealt (Weight, relative) and how the card
 *  looks (frame colour and width, a glow wash, the label on the card). Rows in Config/DefaultGame.ini +CardRarities. */
USTRUCT(BlueprintType)
struct FDCCardRarity
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Weight = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FText Label;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float FramePx = 3.f;
	/** Alpha of a wash of Color over the card (0 = none): rarer cards glow. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Glow = 0.f;
};

/** Satirical headline rows (R22): lines for one photo category (ANY = fallback, CORRECTION = a wiped run). Tokens:
 *  {PLAYER}, {SECTOR}, {COSTAR}. */
USTRUCT(BlueprintType)
struct FDCHeadlineRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Category;
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FString> Lines;
};

namespace DCRunRules
{
	/** A headline for a photo (R22): the first of its categories with rows (else ANY), a seeded line not in Used
	 *  (the session's printed lines; a category whose lines are all used starts over), tokens filled. Adds it to Used. */
	EPIC_AI_MEGA_JAM2_API FString PickHeadline(int32 Seed, const TArray<FDCHeadlineRow>& Rows, const TArray<FName>& Categories, const FString& Player,
		const FString& Sector, const FString& CoStar, TSet<FString>& Used);

	/** The draft (R10): every option still open, grouped by pool in declared order, at most PerPool per pool
	 *  (a seeded pick when a pool has more). Skips ids in Taken. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> DraftOffer(int32 Seed, const TArray<FDCUpgradeDef>& Upgrades, const TArray<FName>& Taken, int32 PerPool = 3);

	/** The draft hand (R21): HandSize distinct cards from every open option (any pool), each drawn with the weight of its
	 *  rarity row (unknown rarity: weight 1), without replacement. Seeded: the host deals and the hand replicates. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> DraftHand(int32 Seed, const TArray<FDCUpgradeDef>& Upgrades, const TArray<FDCCardRarity>& Rarities, const TArray<FName>& Taken, int32 HandSize, FName MaxRarity = NAME_None);

	/** Does this graded photo satisfy the scoop? */
	EPIC_AI_MEGA_JAM2_API bool ScoopMet(const FDCScoopDef& Scoop, const FDCPhotoInputs& In, const FDCPhotoGrade& Grade);

	/** Next scoop index, never the same as Last when there is a choice. */
	EPIC_AI_MEGA_JAM2_API int32 PickScoop(FRandomStream& Rng, int32 NumScoops, int32 Last);

	/** Photo indices, best critic score first (stable: earlier shot wins a tie). */
	EPIC_AI_MEGA_JAM2_API TArray<int32> CriticOrder(const TArray<FDCPhotoRecord>& Photos);

	/** Adds NewId to a critic-sorted list of photo ids at its critic rank (a late second take is ranked, not appended). */
	EPIC_AI_MEGA_JAM2_API void InsertByCritic(TArray<int32>& Choices, const TArray<FDCPhotoRecord>& Photos, int32 NewId);

	EPIC_AI_MEGA_JAM2_API int32 PageScore(int32 CriticScore, int32 Votes, int32 VoteBonus);

	/** Index of the headline pick (highest page score, earliest on a tie); -1 when empty. */
	EPIC_AI_MEGA_JAM2_API int32 HeadlineIndex(const TArray<FDCPagePick>& Picks);

	/** Fills {SUBJECT} {SECTOR} {PLAYER} {TIMING} in a seeded headline template. */
	EPIC_AI_MEGA_JAM2_API FString MakeHeadline(int32 Seed, const TArray<FString>& Templates, const TMap<FName, FString>& Subjects,
		const FDCPhotoGrade& Grade, const FString& SectorName, const FString& Player);
}
