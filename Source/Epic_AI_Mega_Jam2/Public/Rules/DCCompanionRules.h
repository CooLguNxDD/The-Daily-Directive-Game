// R22 card companions (human 2026-11-15): drones that orbit a photographer and turrets the player sets down, both
// shooting the nearest hostile in range. Pure rules (no world): where a companion stands (a mover registry keyed by
// the row's Mover), which target it picks, where a deploy lands. ADCCompanion (DCCompanion.cpp) is the actor; the
// rows are +Companions in Config/DefaultGame.ini. Tested under -nullrhi (CompanionTest.cpp).
#pragma once

#include "CoreMinimal.h"
#include "DCCompanionRules.generated.h"

/** A companion kind: how it moves, how it shoots, how it looks. */
USTRUCT(BlueprintType)
struct FDCCompanionDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Companion") FName Id;
	/** Mover registry id: Orbit (circles its owner) or Fixed (stays where it was set down). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") FName Mover = TEXT("Orbit");
	/** True: anchored on the owner's pawn (a drone); false: on the spot it was set down (a turret). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") bool bFollowsOwner = true;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float OrbitRadiusCm = 220.f;
	/** Height above the anchor (the owner's centre, or the ground under a fixed companion). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float HeightCm = 160.f;
	/** Seconds per lap (Orbit); several companions of one owner spread evenly round the lap. */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float OrbitSeconds = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float BobCm = 15.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float BobHz = 0.7f;
	/** The gun above the companion's origin (a turret's barrel over its base). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float MuzzleCm = 0.f;
	/** Shooting: nearest crew-hostile unit within RangeCm with a clear line, one shot every FireSeconds. */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float RangeCm = 2500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float FireSeconds = 0.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float Damage = 20.f;
	/** Seconds it lives once placed (0 = until the sector ends). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float LifeSeconds = 0.f;
	/** Deploy (Fixed): set down this far ahead of the player, on ground at most MaxStepCm above or below their feet;
	 *  at most MaxAlive of one owner at once (the oldest is recalled); no deploy within BlockNearDownedCm of a downed
	 *  teammate (the same key revives). */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float PlaceDistanceCm = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float MaxStepCm = 150.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") int32 MaxAlive = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float BlockNearDownedCm = 350.f;
	/** Look: a voxel-forge model (none = a primitive in Color), its scale, the tracer colour, the shot cue. */
	UPROPERTY(EditAnywhere, Config, Category = "Companion") FName Model;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float Scale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") FLinearColor Color = FLinearColor(0.3f, 0.55f, 1.f);
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float Glow = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") FName ShotSound = TEXT("FactionShot");
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float ShotVolume = 0.4f;
	UPROPERTY(EditAnywhere, Config, Category = "Companion") float ShotPitch = 1.3f;
};

namespace DCCompanionRules
{
	/** Where the companion stands now. Anchor: the owner's centre (Orbit) or the spot it was set down (Fixed). Slot
	 *  of Slots spreads companions of one owner round the lap. An unknown mover stays on the anchor. */
	EPIC_AI_MEGA_JAM2_API FVector Position(const FDCCompanionDef& Def, const FVector& Anchor, float Now, int32 Slot, int32 Slots);
	EPIC_AI_MEGA_JAM2_API bool IsKnownMover(FName Mover);

	/** Index of the nearest candidate within RangeCm of From (-1 = none). */
	EPIC_AI_MEGA_JAM2_API int32 PickTarget(const FVector& From, float RangeCm, const TArray<FVector>& Candidates);

	/** The spot a deploy aims at: PlaceDistanceCm ahead of Feet along Forward (ground plane). */
	EPIC_AI_MEGA_JAM2_API FVector PlaceAim(const FDCCompanionDef& Def, const FVector& Feet, const FVector& Forward);
	/** Is ground at GroundZ under the aim a valid deploy for a player standing at FeetZ? */
	EPIC_AI_MEGA_JAM2_API bool PlaceOk(const FDCCompanionDef& Def, float FeetZ, float GroundZ);
}
