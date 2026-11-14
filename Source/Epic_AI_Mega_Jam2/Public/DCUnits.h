// Live unit list per world (R3, advisor a-2): enemies register on BeginPlay and leave on EndPlay, so targeting walks
// one short array instead of TActorRange every tick. Read by the host's foe search and the faction directors.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
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

private:
	UPROPERTY() TArray<TObjectPtr<ADCEnemy>> Units;
};
