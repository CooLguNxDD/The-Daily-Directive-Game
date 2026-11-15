// R13 remix as data: one +CueMix row per cue that needs a level trim or a remixed recording. The sound class
// (+SoundClasses) still decides voices, priority and pitch; this row only sets the cue's own level inside its class and,
// optionally, a remixed USoundWave (Content/DC/Audio/S_Sfx_<Cue>) the audio subsystem plays instead of the synth wave.
// Trims come from the solo cue-roll measurement (tools/evidence/sfx_levels.py): every cue within about 6 dB of the
// median loudness, strikes and collapses a little above.
#pragma once

#include "CoreMinimal.h"
#include "DCCueMix.generated.h"

USTRUCT(BlueprintType)
struct FDCCueMix
{
	GENERATED_BODY()
	/** The cue (code synth or +SoundRecipes name). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") FName Cue;
	/** Level trim times the request and class volume (1 = as rendered). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float Volume = 1.f;
	/** Optional remixed recording played instead of the synth wave (empty = the synth cue). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound", meta = (AllowedClasses = "/Script/Engine.SoundWave")) FSoftObjectPath Sound;
};

namespace DCCueMix
{
	/** The row of a cue, or null. */
	EPIC_AI_MEGA_JAM2_API const FDCCueMix* Find(const TArray<FDCCueMix>& Rows, FName Cue);
	/** The cue's trim (1 without a row), clamped to 0..4 (untrusted data). */
	EPIC_AI_MEGA_JAM2_API float Trim(const TArray<FDCCueMix>& Rows, FName Cue);
}
