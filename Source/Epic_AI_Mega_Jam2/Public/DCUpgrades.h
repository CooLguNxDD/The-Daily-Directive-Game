// Upgrade effect registry (R10): each draft option names an Effect id; Apply looks it up and runs it on the host.
// A new kind of upgrade is a new row in the table plus its data in Config/DefaultGame.ini.
#pragma once

#include "CoreMinimal.h"

class ADCGameState;
class ADCPlayerState;
struct FDCUpgradeDef;

namespace DCUpgrades
{
	EPIC_AI_MEGA_JAM2_API bool Apply(ADCGameState& GS, ADCPlayerState& PS, const FDCUpgradeDef& Def);
	EPIC_AI_MEGA_JAM2_API bool IsKnownEffect(FName Effect);
}
