// Upgrade effect registry (R10): each draft option names an Effect id; Apply looks it up and runs it on the host.
// A new kind of upgrade is a new row in the table plus its data in Config/DefaultGame.ini.
// R22 card kinds: an effect may also act later in the run through optional hooks (sector start, the Interact press,
// its owner going down); the game mode and the character call the walkers below, which run every owned card's hook.
#pragma once

#include "CoreMinimal.h"

class ADCCharacter;
class ADCGameMode;
class ADCGameState;
class ADCPlayerState;
struct FDCUpgradeDef;

namespace DCUpgrades
{
	EPIC_AI_MEGA_JAM2_API bool Apply(ADCGameState& GS, ADCPlayerState& PS, const FDCUpgradeDef& Def);
	EPIC_AI_MEGA_JAM2_API bool IsKnownEffect(FName Effect);

	/** Host, every sector start: each owned card's SectorStart hook (launch drones, deal deploy charges). */
	EPIC_AI_MEGA_JAM2_API void SectorStart(ADCGameMode& GM, ADCPlayerState& PS);
	/** Host, the player pressed Interact standing in a live sector: the first owned card whose hook acts wins. */
	EPIC_AI_MEGA_JAM2_API bool Interact(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player);
	/** Host, the player went down: each owned card's Downed hook. */
	EPIC_AI_MEGA_JAM2_API void Downed(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player);
	/** Host, the player called a card's entry in the call-in list (a +Stratagems row with Effect="Card") at Target:
	 *  the owned card's Call hook acts and spends a charge. False = refused (not owned, no charge, bad ground). */
	EPIC_AI_MEGA_JAM2_API bool CallCard(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, FName CardId, const FVector& Target);
	/** A FDCUpgradeDef::CallIn value the call-in registry knows (None, Active, Status). */
	EPIC_AI_MEGA_JAM2_API bool IsKnownCallIn(FName CallIn);
}
