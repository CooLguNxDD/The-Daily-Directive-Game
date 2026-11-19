// Live unit list per world (R3, advisor a-2): enemies register on BeginPlay and leave on EndPlay, so targeting walks
// one short array instead of TActorRange every tick. Read by the host's foe search and the faction directors.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Rules/DCFactionRules.h"
#include "DCUnits.generated.h"

class ADCEnemy;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCUnits : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDCUnits* Get(const UObject* WorldContext);
	void Add(ADCEnemy* Unit) { Units.AddUnique(Unit); }
	void Remove(ADCEnemy* Unit) { Units.RemoveSwap(Unit); }
	const TArray<TObjectPtr<ADCEnemy>>& All() const { return Units; }
	/** Live units of a faction. */
	int32 CountAlive(FName Faction) const;
	/** R3, one owner (advisor a-9): a live unit of a hostile faction within RangeCm of this one (the CLASH tag and the
	 *  pilot read it). Returns that unit, or null. Any machine can ask: factions and positions replicate. */
	const ADCEnemy* InClash(const ADCEnemy& Unit, float RangeCm = 1500.f) const;
	/** Host, one owner of crew attack slots (advisor a-17): may this unit attack this crew member now (takes or renews
	 *  a lease)? CanEngage is the read-only check the foe search uses. */
	bool TryEngage(const AActor& Victim, const ADCEnemy& Unit, bool bRanged, float Now);
	bool CanEngage(const AActor& Victim, const ADCEnemy& Unit, bool bRanged, float Now) const;
	/** Host: how much of a unit hit on this crew member lands under its damage budget (advisor a-18). */
	float SpendCrewDamage(const AActor& Victim, float Amount, float Now);

private:
	int32 SlotCap(bool bRanged) const;
	UPROPERTY() TArray<TObjectPtr<ADCEnemy>> Units;
	/** Leases per crew member (by actor unique id), melee and ranged. */
	TMap<uint32, TArray<FDCAttackSlot>> MeleeSlots, RangedSlots;
	TMap<uint32, FDCDamageBucket> DamageBuckets;
};
