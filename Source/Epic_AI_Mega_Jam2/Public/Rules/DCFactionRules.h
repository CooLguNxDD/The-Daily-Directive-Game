// Enemy factions (R3), pure: no world, no engine objects. Factions are data rows (+Factions in Config/DefaultGame.ini):
// their units, how likely they own a map or raid it, whom they fight, and the destruction they call in. Each sector
// (map) rolls one main faction with a big share of the spawns and a few invaders with small shares from the run seed,
// so host and clients agree and a replay of the seed plays the same.
#pragma once

#include "CoreMinimal.h"
#include "DCFactionRules.generated.h"

/** One unit kind a faction fields (an +Enemies row id) and its spawn weight inside the faction. */
USTRUCT(BlueprintType)
struct FDCFactionUnit
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FName Enemy;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float Weight = 1.f;
};

USTRUCT(BlueprintType)
struct FDCFactionDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FName Id;
	/** Sector card and HUD name (ROBOTS, THE DEAD...). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FString DisplayName;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<FDCFactionUnit> Units;
	/** Weight to be a map's main faction (0 = never owns a map, e.g. the friendly army). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float MainWeight = 1.f;
	/** Weight to be picked as an invader (0 = never raids). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float InvaderWeight = 1.f;
	/** Factions it fights on sight; "Crew" is the players. A pair is hostile when either side lists the other. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<FName> Hostile;
	/** Squad size per drop. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 SquadMin = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 SquadMax = 6;
	/** Faction-made destruction (advisor a-2): a +Stratagems row the host calls on the tower nearest the faction's
	 *  squad, at most once per StrikeInterval seconds per faction (0 = never). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FName StrikeStratagem;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float StrikeInterval = 0.f;
	/** Telegraph toast every player sees ({M} = metres away). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FString StrikeToast;
	/** Played at a unit's feet on every machine when it appears (a voxel Fx emitter id; None = nothing). The Hive
	 *  bursts out of the street in dust. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FName SpawnFx;
	/** The one light cue of the faction (HUD tag, sector card rule, tracer colour). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FLinearColor Cue = FLinearColor::White;
};

/** How a map's mix is rolled (one Mix=(...) row in Config). */
USTRUCT(BlueprintType)
struct FDCFactionMixRule
{
	GENERATED_BODY()
	/** Invader factions per map (fewer when not enough factions can invade). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 Invaders = 2;
	/** Share of hostile drops that belong to the main faction; the invaders split the rest by their weights. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float MainShare = 0.72f;
	/** Seconds between friendly squads (factions hostile to nobody in Crew) once the sector is live (0 = none). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float AllyGapSeconds = 45.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 AllyMaxAlive = 6;
	/** Global floor between any two faction strikes (seconds) and the body budget they leave free. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float StrikeGapSeconds = 16.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float StrikeStartSeconds = 35.f;
	/** Faction strikes wait while the city's physics body load is above this (the crew's shots own the body budget). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float StrikeMaxBodyLoad = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 SpawnsPerFrame = 2;
	/** Territory: the map splits into ZonesPerSide^2 zones, all the main faction's at the start; every
	 *  TerritorySeconds a zone goes to the faction with the most live units in it, if it outnumbers the owner there. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 ZonesPerSide = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float TerritorySeconds = 2.f;
	/** Units a faction needs in a zone to take it at all. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 TakeMinUnits = 2;
};

/** One map's factions: the owner and its raiders with their shares of the hostile drops (sum 1). */
USTRUCT(BlueprintType)
struct FDCFactionMix
{
	GENERATED_BODY()
	UPROPERTY() FName Main;
	UPROPERTY() TArray<FName> Invaders;
	UPROPERTY() TArray<float> InvaderShares;
	UPROPERTY() float MainShare = 1.f;
	/** Allied factions (never hostile to the crew) that act on their own on this map. */
	UPROPERTY() TArray<FName> Allies;
};

namespace DCFactionRules
{
	/** The players' side in Hostile lists. */
	EPIC_AI_MEGA_JAM2_API extern const FName Crew;

	EPIC_AI_MEGA_JAM2_API const FDCFactionDef* Find(const TArray<FDCFactionDef>& Factions, FName Id);
	/** Hostile when either side lists the other (Crew included). Same faction is never hostile. */
	EPIC_AI_MEGA_JAM2_API bool IsHostile(const TArray<FDCFactionDef>& Factions, FName A, FName B);
	/** A faction that fights the crew. */
	EPIC_AI_MEGA_JAM2_API bool FightsCrew(const TArray<FDCFactionDef>& Factions, FName Id);
	/** Rolls a map's mix from the run seed and the sector index (weights in the rows, counts in the rule). */
	EPIC_AI_MEGA_JAM2_API FDCFactionMix RollMix(const TArray<FDCFactionDef>& Factions, const FDCFactionMixRule& Rule, int32 Seed, int32 Sector);
	/** Which hostile faction a drop belongs to, from a 0..1 roll. */
	EPIC_AI_MEGA_JAM2_API FName PickDropFaction(const FDCFactionMix& Mix, float Roll);
	/** Zone index of a point on a ZonesPerSide grid over [-HalfExtent, HalfExtent]^2 (clamped to the edge zones). */
	EPIC_AI_MEGA_JAM2_API int32 ZoneOf(const FVector& At, float HalfExtent, int32 ZonesPerSide);
	/** One territory step: Units are (position, faction) of live units; a zone flips to the faction with the most units
	 *  there when it has at least MinUnits and strictly more than the owner has there. Returns zones that flipped. */
	EPIC_AI_MEGA_JAM2_API int32 StepTerritory(TArray<FName>& Owners, const TArray<TPair<FVector, FName>>& Units, float HalfExtent, int32 ZonesPerSide, int32 MinUnits);
	/** Which unit of the faction, from a 0..1 roll (None when it has none). */
	EPIC_AI_MEGA_JAM2_API FName PickUnit(const FDCFactionDef& Faction, float Roll);
}
