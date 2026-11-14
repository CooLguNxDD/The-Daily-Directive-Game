// Builds an outfit's voxel parts onto a mannequin (R23): the character in play and the menu preview use this one path.
#pragma once

#include "CoreMinimal.h"
#include "Rules/DCOutfit.h"

class USkeletalMeshComponent;
class UProceduralMeshComponent;

namespace DCOutfitBuild
{
	/** Replaces Out with one procedural mesh per worn part on Mesh (bones hidden where a part covers them).
	 *  Returns the camera slot's component (or null), so the owner can show it only while the camera is not in hand. */
	EPIC_AI_MEGA_JAM2_API UProceduralMeshComponent* Apply(USkeletalMeshComponent* Mesh, const FDCOutfit& Outfit, TArray<TObjectPtr<UProceduralMeshComponent>>& Out);
	/** Decodes a share code against the shipped data (default outfit on any error). */
	EPIC_AI_MEGA_JAM2_API FDCOutfit FromCode(const FString& Code);
}
