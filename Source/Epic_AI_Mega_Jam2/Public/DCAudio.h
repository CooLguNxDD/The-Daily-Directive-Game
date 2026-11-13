// Runtime-synthesised sound bank: every cue is a small recipe (code, readable in review) rendered to PCM once
// when the game instance starts, then played through USoundWaveProcedural. No audio assets needed.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DCAudio.generated.h"

class USoundAttenuation;
class UAudioComponent;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch);
	void Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch);
	/** Starts (or keeps) the looping music bed. */
	void StartMusic(const UObject* WorldContext, FName Cue, float Volume);
	void SetMusicVolume(float Volume);

	bool HasCue(FName Cue) const { return Bank.Contains(Cue); }
	int32 NumCues() const { return Bank.Num(); }
	float CueSeconds(FName Cue) const;

	static constexpr int32 SampleRate = 44100;

	/** Listening evidence (-dcaudiorec=Dir): records the master mix to a wav and logs every cue with its time (DCCUE). */
	void StartEvidenceRecording(const UObject* WorldContext);
	void StopEvidenceRecording(const UObject* WorldContext);

private:
	class USoundWaveProcedural* MakeWave(FName Cue) const;

	TMap<FName, TSharedPtr<TArray<uint8>>> Bank;
	UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	FName MusicCue;
	void LogCue(const UObject* WorldContext, FName Cue, const FVector* Location) const;
	double RecordStart = -1.0;
	FString RecordDir;
};

namespace DCAudio
{
	EPIC_AI_MEGA_JAM2_API void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
	EPIC_AI_MEGA_JAM2_API void Play2D(const UObject* WorldContext, FName Cue, float Volume = 1.f, float Pitch = 1.f);

	/** Pure synthesis, used by the subsystem and the tests: the registered cue names. */
	EPIC_AI_MEGA_JAM2_API TArray<FName> CueNames();
	/** Renders one cue to mono 16-bit samples; empty when the cue is unknown. */
	EPIC_AI_MEGA_JAM2_API TArray<int16> Render(FName Cue);
}
