// Pure run rules: draft offers, HQ Scoop checks, the critic's sort, page scores and headlines. No world, no
// rendering; the game mode feeds them data and applies the results. Tested under -nullrhi.
#pragma once

#include "CoreMinimal.h"
#include "DCTypes.h"
#include "DCRunRules.generated.h"

struct FDCUpgradeDef;
struct FDCScoopDef;

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
