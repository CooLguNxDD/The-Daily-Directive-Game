// Pure run rules: draft offers, HQ Scoop checks, the critic's sort, page scores and headlines. No world, no
// rendering; the game mode feeds them data and applies the results. Tested under -nullrhi.
#pragma once

#include "CoreMinimal.h"
#include "DCTypes.h"

struct FDCUpgradeDef;
struct FDCScoopDef;

namespace DCRunRules
{
	/** One option per pool (pools in declared order), seeded, skipping ids in Taken. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> DraftOffer(int32 Seed, const TArray<FDCUpgradeDef>& Upgrades, const TArray<FName>& Taken);

	/** Does this graded photo satisfy the scoop? */
	EPIC_AI_MEGA_JAM2_API bool ScoopMet(const FDCScoopDef& Scoop, const FDCPhotoInputs& In, const FDCPhotoGrade& Grade);

	/** Next scoop index, never the same as Last when there is a choice. */
	EPIC_AI_MEGA_JAM2_API int32 PickScoop(FRandomStream& Rng, int32 NumScoops, int32 Last);

	/** Photo indices, best critic score first (stable: earlier shot wins a tie). */
	EPIC_AI_MEGA_JAM2_API TArray<int32> CriticOrder(const TArray<FDCPhotoRecord>& Photos);

	EPIC_AI_MEGA_JAM2_API int32 PageScore(int32 CriticScore, int32 Votes, int32 VoteBonus);

	/** Index of the headline pick (highest page score, earliest on a tie); -1 when empty. */
	EPIC_AI_MEGA_JAM2_API int32 HeadlineIndex(const TArray<FDCPagePick>& Picks);

	/** Fills {SUBJECT} {SECTOR} {PLAYER} {TIMING} in a seeded headline template. */
	EPIC_AI_MEGA_JAM2_API FString MakeHeadline(int32 Seed, const TArray<FString>& Templates, const TMap<FName, FString>& Subjects,
		const FDCPhotoGrade& Grade, const FString& SectorName, const FString& Player);
}
