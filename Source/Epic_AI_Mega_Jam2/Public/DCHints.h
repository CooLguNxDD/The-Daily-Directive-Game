// R25 hint triggers: a registry of named conditions over the local player's view of the game. A +Hints row names one by
// its Id; adding a hint kind is a registry entry here plus a data row (no branching in the controller).
#pragma once

#include "CoreMinimal.h"

class ADCPlayerController;

namespace DCHints
{
	/** Does the trigger named Id hold right now for this local player? Unknown ids never hold. */
	EPIC_AI_MEGA_JAM2_API bool Holds(FName Id, const ADCPlayerController& PC);
	EPIC_AI_MEGA_JAM2_API bool IsKnown(FName Id);
}
