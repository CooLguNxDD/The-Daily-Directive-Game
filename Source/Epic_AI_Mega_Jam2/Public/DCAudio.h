// Runtime-synthesised sound bank: every cue is a small recipe (code, readable in review) rendered to PCM once
// when the game instance starts, then played through USoundWaveProcedural. No audio assets needed.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Rules/DCSoundQueue.h"
#include "DCAudio.generated.h"

class USoundAttenuation;
class UAudioComponent;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch);
	void Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch);
	/** Starts (or keeps) the looping music bed. */
	void StartMusic(const UObject* WorldContext, FName Cue, float Volume);
	void SetMusicVolume(float Volume);
	/** Re-applies the saved master volume to the playing music (settings change, R24). */
	void ApplyMasterVolume(const UObject* WorldContext);

	bool HasCue(FName Cue) const { return Bank.Contains(Cue); }
	int32 NumCues() const { return Bank.Num(); }
	float CueSeconds(FName Cue) const;

	static constexpr int32 SampleRate = 44100;

	/** Listening evidence (-dcaudiorec=Dir): records the master mix to a wav and logs every cue with its time (DCCUE). */
	void StartEvidenceRecording(const UObject* WorldContext);
	void StopEvidenceRecording(const UObject* WorldContext);

private:
	class USoundWaveProcedural* MakeWave(FName Cue) const;
	/** R13: a pooled wave for the cue whose last play has ended (refilled), a new pool entry, or null when the pool is full. */
	class USoundWaveProcedural* AcquireWave(FName Cue, float Pitch, int32 Priority);
	/** R13: once per frame: merge, budget and start the queued plays (DCAUDIO lines log what was dropped). */
	bool FlushQueue(float DeltaSeconds);
	void Start(const FDCSoundRequest& R);

	TArray<FDCSoundRequest> Queue;
	TArray<FDCSoundVoice> Voices;
	UPROPERTY() TArray<TObjectPtr<class USoundWaveProcedural>> PoolWaves;
	TArray<double> PoolFreeAt;
	TMap<FName, TArray<int32>> PoolOf;
	FTSTicker::FDelegateHandle FlushHandle;
	int32 Dropped = 0;
	int32 MergedCount = 0;
	/** R13 remix (h-audio +CueMix rows): remixed recordings loaded once at Initialize, played instead of the synth wave. */
	UPROPERTY() TMap<FName, TObjectPtr<class USoundBase>> MixSounds;
	/** Ducking state: until DuckUntil (real seconds), plays of classes below DuckPriority are scaled by DuckScale. */
	double DuckUntil = 0.0;
	int32 DuckPriority = 0;
	float DuckScale = 1.f;

	TMap<FName, TSharedPtr<TArray<uint8>>> Bank;
	UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	/** The music level a caller asked for, before the master volume. */
	float MusicBase = 1.f;
	FName MusicCue;
	void LogCue(const UObject* WorldContext, FName Cue, const FVector* Location) const;
	double RecordStart = -1.0;
	FString RecordDir;
};

namespace DCAudio
{
	EPIC_AI_MEGA_JAM2_API void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
	EPIC_AI_MEGA_JAM2_API void Play2D(const UObject* WorldContext, FName Cue, float Volume = 1.f, float Pitch = 1.f);
	/** The saved master volume (R24), 0..1. */
	EPIC_AI_MEGA_JAM2_API float Master(const UObject* WorldContext);

	/** Pure synthesis, used by the subsystem and the tests: the registered cue names. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> CueNames();
	/** Renders one cue to mono 16-bit samples; empty when the cue is unknown. */
	EPIC_AI_MEGA_JAM2_API TArray<int16> Render(FName Cue);
}
