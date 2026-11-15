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
	/** First sighting (once per run per machine): a short line naming the faction, its cue and whether it is friendly. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FString Callout;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<FDCFactionUnit> Units;
	/** Weight to be a map's main faction (0 = never owns a map, e.g. the friendly army). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float MainWeight = 1.f;
	/** Weight to be picked as an invader (0 = never raids). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float InvaderWeight = 1.f;
	/** Factions it fights on sight; "Crew" is the players. A pair is hostile when either side lists the other. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<FName> Hostile;
	/** Squad size per drop. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 SquadMin = 3;
	/** Pressure packs of this faction are PackScale times the rule's pack size (crowds are crowds), and each live unit
	 *  counts PressureWeight against the pressure's alive target (a shambler is less threat than a trooper). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float PackScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float PressureWeight = 1.f;
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
	/** Nests: at sector start, NestCount units of NestUnit (a Behaviour="Nest" row) burst up in the ring around the
	 *  crew when the faction is on the map (main or invader). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") FName NestUnit;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 NestCount = 0;
	/** Night read: one unit in CueLightEvery carries a small shadowless light in the cue colour (0 = none), so a squad
	 *  shows its colour on the street without a light per unit (advisor a-2). Cosmetic, picked per machine. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 CueLightEvery = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CueLightCandela = 120.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CueLightRadiusCm = 500.f;
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
	/** One cap on live faction units of every source (pressure, guards, allies, nests, opening; advisor a-7): a squad
	 *  is clamped to the room left, counting spawns still queued. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 MaxUnitsAlive = 40;
	/** The opening clash (first 10 seconds sell the game): OpeningSeconds into the sector a main-faction squad and an
	 *  invader squad of OpeningSquad each land OpeningApartCm apart in the ring around the crew and fight (0 = off). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float OpeningSeconds = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 OpeningSquad = 4;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float OpeningApartCm = 1800.f;
	/** Drop spread (cm) of the opening clash squads and of the friendly army's squads. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float OpeningSpreadCm = 300.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float AllySpreadCm = 350.f;
	/** Territory: the map splits into ZonesPerSide^2 zones, all the main faction's at the start; every
	 *  TerritorySeconds a zone goes to the faction with the most live units in it, if it outnumbers the owner there. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 ZonesPerSide = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float TerritorySeconds = 2.f;
	/** Units a faction needs in a zone to take it at all. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") int32 TakeMinUnits = 2;
	/** Crew grace (advisor a-10): for CrewGraceSeconds after a sector starts, units only fight the crew inside
	 *  CrewSelfDefenceCm (the opening clash, nests and early raiders fight each other, not the players). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CrewGraceSeconds = 20.f;
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CrewSelfDefenceCm = 600.f;
	/** Ground units never step closer to a standing crew member than their body radius + the capsule radius + this
	 *  margin (unswept moves used to walk into the capsule and pin the player). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CrewContactMarginCm = 25.f;
	/** Unit damage to the crew by crew size (index = players - 1, the last entry for bigger crews): a solo
	 *  photographer has nobody to revive them (packaged 1b41c0c solo: downed at 29 s and 39 s by raider fire). */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<float> CrewDamageByPlayers = {0.45f, 0.75f, 0.9f, 1.f};
	/** Attack slots per crew member (advisor a-17): at most this many melee / ranged units may attack one player at a
	 *  time, by crew size (index = players - 1); a slot is a lease of CrewSlotLeaseSeconds, renewed by each attack.
	 *  Surplus units look for rival-faction foes instead. */
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<int32> CrewMeleeSlotsByPlayers = {3, 4, 5, 6};
	UPROPERTY(EditAnywhere, Config, Category = "Faction") TArray<int32> CrewRangedSlotsByPlayers = {2, 3, 3, 4};
	UPROPERTY(EditAnywhere, Config, Category = "Faction") float CrewSlotLeaseSeconds = 1.5f;
};

/** One attack-slot lease on a crew member (pure rule data, host only). */
struct FDCAttackSlot
{
	uint32 Holder = 0;
	float Until = 0.f;
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
	EPIC_AI_MEGA_JAM2_API FDCFactionMix RollMix(const TArray<FDCFactionDef>& Factions, const FDCFactionMixRule& Rule, int32 Seed, int32 Sector,
		const TArray<FName>& Owners = TArray<FName>());
	/** Which hostile faction a drop belongs to, from a 0..1 roll. */
	EPIC_AI_MEGA_JAM2_API FName PickDropFaction(const FDCFactionMix& Mix, float Roll);
	/** Zone index of a point on a ZonesPerSide grid over [-HalfExtent, HalfExtent]^2 (clamped to the edge zones). */
	EPIC_AI_MEGA_JAM2_API int32 ZoneOf(const FVector& At, float HalfExtent, int32 ZonesPerSide);
	/** One territory step: Units are (position, faction) of live units; a zone flips to the faction with the most units
	 *  there when it has at least MinUnits and strictly more than the owner has there. Returns zones that flipped. */
	EPIC_AI_MEGA_JAM2_API int32 StepTerritory(TArray<FName>& Owners, const TArray<TPair<FVector, FName>>& Units, float HalfExtent, int32 ZonesPerSide, int32 MinUnits);
	/** Which unit of the faction, from a 0..1 roll (None when it has none). */
	EPIC_AI_MEGA_JAM2_API FName PickUnit(const FDCFactionDef& Faction, float Roll);
	/** Attack slots (advisor a-17): drops expired leases, then renews Holder's lease or grants a free one while fewer
	 *  than Cap are held. False = the target is saturated for this holder. */
	EPIC_AI_MEGA_JAM2_API bool GrantSlot(TArray<FDCAttackSlot>& Slots, uint32 Holder, float Now, int32 Cap, float Lease);
	/** Read-only: would GrantSlot succeed now. */
	EPIC_AI_MEGA_JAM2_API bool HasSlot(const TArray<FDCAttackSlot>& Slots, uint32 Holder, float Now, int32 Cap);
}
