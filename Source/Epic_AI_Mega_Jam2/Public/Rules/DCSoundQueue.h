// Pure sound-queue rule (R13, human design 2026-11-14): every PlayAt / Play2D request is queued and flushed once per
// frame. (1) The same cue near the same spot within a short window merges into one louder play instead of stacking;
// (2) each sound class has a voice budget and a per-frame start budget, extra requests wait a few frames or drop, by
// class priority (strikes, UI and player gunfire first; crumbs and far impacts last); (3) pitch is clamped per class.
// No world, no engine objects: the audio subsystem owns the waves and calls Flush with its clock.
#pragma once

#include "CoreMinimal.h"
#include "DCSoundQueue.generated.h"

/** One sound class (a +SoundClasses row in Config/DefaultGame.ini). A cue not listed in any row uses the "Default" row. */
USTRUCT(BlueprintType)
struct FDCSoundClassDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Sound") FName Id;
	/** Cues of this class (code synth or +SoundRecipes names). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") TArray<FName> Cues;
	/** Higher plays first; at or above FDCSoundMixTuning::ProtectedPriority the class ignores the shared voice cap. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 Priority = 50;
	/** Voices of this class sounding at once. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 MaxVoices = 8;
	/** New voices of this class started in one frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 StartsPerFrame = 2;
	/** Frames a request may wait for a free voice before it is dropped. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 MaxWaitFrames = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float PitchMin = 0.92f;
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float PitchMax = 1.08f;
	/** Mixer pass: class level applied to every play. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float Volume = 1.f;
	/** Same cue within this window and radius merges into the earlier play (2D cues merge by window only). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float MergeWindowSeconds = 0.06f;
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float MergeRadiusCm = 800.f;
	/** A merged play gets this much louder (fraction of the quieter request), up to MaxVolume. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float MergeVolumeBoost = 0.3f;
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float MaxVolume = 1.6f;
	/** Ducking (human 2026-11-14: other sounds dip under a strike): when this class starts a voice, lower-priority
	 *  plays that start within DuckSeconds are scaled by (1 - DuckOthers). 0 = no ducking. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float DuckOthers = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float DuckSeconds = 0.6f;
};

/** Global mix limits (SoundMix=(...) in Config/DefaultGame.ini). */
USTRUCT(BlueprintType)
struct FDCSoundMixTuning
{
	GENERATED_BODY()
	/** Voices of all unprotected classes at once (kept under the engine's AudioMaxChannels). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 TotalVoices = 40;
	/** Classes at or above this priority are never held back by TotalVoices (only by their own budget). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 ProtectedPriority = 80;
	/** Pooled procedural waves per cue (reused once their play has ended). */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") int32 PoolPerCue = 8;
	/** A pooled wave is reused this long after its play ended. */
	UPROPERTY(EditAnywhere, Config, Category = "Sound") float ReuseGuardSeconds = 0.15f;
};

struct FDCSoundRequest
{
	FName Cue;
	FVector Location = FVector::ZeroVector;
	bool b2D = false;
	float Volume = 1.f;
	float Pitch = 1.f;
	int32 WaitedFrames = 0;
	int32 Merged = 0;
};

struct FDCSoundVoice
{
	FName Cue;
	int32 Class = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	bool b2D = false;
	double StartTime = 0.0;
	double EndTime = 0.0;
};

enum class EDCSoundDrop : uint8
{
	Merged,
	Budget,
};

struct FDCSoundDropped
{
	FDCSoundRequest Request;
	EDCSoundDrop Why = EDCSoundDrop::Budget;
};

namespace DCSoundQueue
{
	/** Index of the cue's class row: the row listing it, else the "Default" row, else INDEX_NONE (plays unlimited). */
	EPIC_AI_MEGA_JAM2_API int32 ClassOf(const TArray<FDCSoundClassDef>& Classes, FName Cue);

	/**
	 * One frame's flush. Pending keeps the requests that wait; Voices gains the started plays and loses the ended ones.
	 * Plays carry the final volume (class level and merge boost applied) and the clamped pitch.
	 */
	EPIC_AI_MEGA_JAM2_API void Flush(const TArray<FDCSoundClassDef>& Classes, const FDCSoundMixTuning& Mix, double Now,
		TFunctionRef<float(FName)> CueSeconds, TArray<FDCSoundRequest>& Pending, TArray<FDCSoundVoice>& Voices,
		TArray<FDCSoundRequest>& OutPlay, TArray<FDCSoundDropped>& OutDrop);
}
