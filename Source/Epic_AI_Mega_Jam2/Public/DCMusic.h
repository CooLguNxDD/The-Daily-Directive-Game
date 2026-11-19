// R14 music player: local to each machine, host-agnostic. A few times a second it reads replicated state (the run
// phase, live crew-hostile units near the local camera, the game state's blast records), turns it into a combat
// intensity (DCMusicRules) and crossfades between imported looping tracks. What plays where, the thresholds and the
// fade times are data: Music=, +MusicTracks, +MusicLayers and +MusicPhases in Config/DefaultGame.ini.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "DCTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DCMusic.generated.h"

class UAudioComponent;
class USoundBase;
class ADCGameState;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCMusicSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDCMusicSubsystem* Get(const UObject* WorldContext);

	virtual void Deinitialize() override;

	/** Ready phase, from the local player controller's BeginPlay: loads the +MusicTracks sounds once and starts
	 *  sampling. False = no track could be loaded (the caller keeps the synth music bed). */
	bool Start(const UObject* WorldContext);
	/** Evidence and cue rolls: scales the whole score (0 = silent), applied at once. */
	void SetDuck(float InDuck);

	float GetIntensity() const { return Intensity; }
	FName GetPlaying() const { return Playing; }
	bool IsStarted() const { return TickHandle.IsValid(); }

private:
	bool Tick(float DeltaSeconds);
	void Sample(UWorld& World, const ADCGameState& GS, float Dt);
	/** Raw intensity and what it saw (for the log). */
	float ReadIntensity(UWorld& World, const ADCGameState& GS, int32& OutHostiles, int32& OutBlasts) const;
	/** The track the phase wants now and the fade into it (intensity layer or playlist slot). */
	FName PickTrack(const struct FDCMusicPhase& Phase, float Dt, float& InOutFade);
	/** Crossfades to Track (None = silence); tracks in Keep stay running silently so a layer returns in time. */
	void SetTarget(UWorld& World, FName Track, float Fade, const TArray<FName>& Keep);
	void ApplyLevels();
	float TrackLevel(FName Track) const;
	float TrackSeconds(FName Track) const;

	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;
	UPROPERTY() TMap<FName, TObjectPtr<UAudioComponent>> Decks;
	/** Decks still fading out after their track was asked for again (held so they finish). */
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> Tails;

	FTSTicker::FDelegateHandle TickHandle;
	FName Playing;
	float Intensity = 0.f;
	/** Real time of the last sample: the core ticker fires at SampleHz but hands over the frame delta (R14 bug: the
	 *  smoothing ran ~30x slow, so the fight layer came late and never left). */
	double LastSampleReal = 0.0;
	float Raw = 0.f;
	int32 Layer = INDEX_NONE;
	float LayerHeld = 0.f;
	int32 Slot = 0;
	float SlotElapsed = 0.f;
	/** The phase the music last entered; 255 = none yet. */
	EDCRunPhase Phase = static_cast<EDCRunPhase>(255);
	float Duck = 1.f;
	float AppliedMaster = -1.f;
	double NextLog = 0.0;
	int32 LastHostiles = 0;
	int32 LastBlasts = 0;
};
