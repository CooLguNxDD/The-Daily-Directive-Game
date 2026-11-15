// Pure music rules (R14): no world, no engine objects. Every machine runs them on its own view of replicated state
// (live hostile units near the local camera, the game state's blast records), so music is local and host-agnostic.
// Combat intensity is a 0..1 number; music layers (+MusicLayers, ascending EnterAt) are picked from it with
// hysteresis: a layer is entered at EnterAt, left only when intensity falls to its ExitAt and it played MinHoldSeconds.
#pragma once

#include "CoreMinimal.h"
#include "DCTypes.h"
#include "DCMusicRules.generated.h"

/** One music file (an imported looping USoundWave, see tools/audio/ and ASSETS.md). */
USTRUCT(BlueprintType)
struct FDCMusicTrack
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Music") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Music", meta = (AllowedClasses = "/Script/Engine.SoundBase")) FSoftObjectPath Sound;
	/** Track trim (0..1), times Music.Volume and the saved master volume. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float Volume = 1.f;
	/** A synth recipe id (DCAudio's recipe registry, e.g. March) played as a seamless loop instead of Sound. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") FName Synth;
};

/** One combat-intensity layer of the in-sector music. */
USTRUCT(BlueprintType)
struct FDCMusicLayer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Music") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Music") FName Track;
	/** Intensity at which this layer takes over (layers are listed in ascending EnterAt; the first is 0). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float EnterAt = 0.f;
	/** Intensity at or below which this layer gives way to a lower one (hysteresis; below EnterAt). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float ExitAt = 0.f;
	/** Crossfade seconds into this layer. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float FadeInSeconds = 3.f;
	/** Seconds this layer plays at least before it may give way to a lower one. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float MinHoldSeconds = 6.f;
};

/** What plays in a run phase: a playlist (menu set, crossfaded track to track) or the intensity layers. */
USTRUCT(BlueprintType)
struct FDCMusicPhase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Music") EDCRunPhase Phase = EDCRunPhase::Lobby;
	/** Tracks played in turn (each for PlaylistSeconds or its length), crossfading by Music.PlaylistFadeSeconds. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") TArray<FName> Playlist;
	/** True: the +MusicLayers pick by combat intensity (the playlist is ignored). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") bool bByIntensity = false;
	/** Crossfade seconds when the music changes into this phase. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float FadeInSeconds = 3.f;
};

/** Music= in Config/DefaultGame.ini: how combat intensity is read and how the player mixes. */
USTRUCT(BlueprintType)
struct FDCMusicTuning
{
	GENERATED_BODY()
	/** Music bus level (times the saved master volume and each track's trim). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float Volume = 0.35f;
	/** How often the player re-reads the world (Hz); the fades are the audio engine's, so this can stay low. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float SampleHz = 4.f;
	/** Live crew-hostile units within this distance (cm) of the local camera count. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float HostileRadiusCm = 5000.f;
	/** That many hostiles near = the full hostile share. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float HostilesForFull = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Music") float HostileWeight = 0.7f;
	/** Blasts and collapses (game-state records) within this distance (cm) of the camera count. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float BlastRadiusCm = 12000.f;
	/** A blast's share fades to 0 over this many seconds after its peak. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float BlastHoldSeconds = 10.f;
	UPROPERTY(EditAnywhere, Config, Category = "Music") float BlastWeight = 0.6f;
	/** Share of BlastWeight while a strike is still telegraphing (the beam is down, the blast is coming). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float TelegraphShare = 0.5f;
	/** Smoothing time constants (s) of the intensity when it rises and when it falls. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float RiseSeconds = 0.4f;
	UPROPERTY(EditAnywhere, Config, Category = "Music") float FallSeconds = 5.f;
	/** Playlist: crossfade seconds from one track to the next, and how long a track plays (0 = its own length). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float PlaylistFadeSeconds = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Music") float PlaylistSeconds = 0.f;
	/** Voice priority of the music (USoundBase::Priority): high, so an SFX flood never culls the score. */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float Priority = 8.f;
	/** Log a DCMUSIC line at least this often (s) while intensity changes (evidence; 0 = only on switches). */
	UPROPERTY(EditAnywhere, Config, Category = "Music") float LogEverySeconds = 1.f;
};

/** One blast as the music reads it: seconds since its peak (negative = still telegraphing) and distance (cm). */
struct FDCMusicBlast
{
	float Age = 0.f;
	float DistanceCm = 0.f;
	/** Seconds since the telegraph began (>= 0 once it is visible). */
	float SinceTelegraph = 0.f;
};

namespace DCMusicRules
{
	/** Raw 0..1 combat intensity from what the local machine sees. */
	EPIC_AI_MEGA_JAM2_API float Intensity(const FDCMusicTuning& T, int32 HostilesNear, TConstArrayView<FDCMusicBlast> Blasts);
	/** One smoothing step from Current toward Raw over Dt seconds (rise and fall time constants). */
	EPIC_AI_MEGA_JAM2_API float Smooth(const FDCMusicTuning& T, float Current, float Raw, float Dt);
	/** The layer to play: escalates at once to the highest layer whose EnterAt <= Intensity; steps down only when the
	 *  current layer held MinHoldSeconds and Intensity is at or below its ExitAt. Current = INDEX_NONE picks fresh. */
	EPIC_AI_MEGA_JAM2_API int32 PickLayer(const TArray<FDCMusicLayer>& Layers, int32 Current, float Intensity, float HeldSeconds);
	/** The seconds one sample advances: the real delta, capped at two sample periods, so the first sample after a
	 *  long load (the ticker reports the whole stall) neither ends a playlist slot nor jumps the intensity. */
	EPIC_AI_MEGA_JAM2_API float StepSeconds(const FDCMusicTuning& T, float DeltaSeconds);
	/** The playlist slot after Slot (wraps; 0 for an empty list). */
	EPIC_AI_MEGA_JAM2_API int32 NextSlot(int32 Slot, int32 Num);
}
