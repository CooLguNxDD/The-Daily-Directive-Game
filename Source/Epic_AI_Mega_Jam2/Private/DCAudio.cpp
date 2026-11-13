#include "DCAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr float Rate = static_cast<float>(UDCAudioSubsystem::SampleRate);

	enum class EWave : uint8 { Sine, Square, Saw, Tri };

	using FWaveFn = float (*)(float Phase);
	const FWaveFn WaveFns[] = {
		[](float P) { return FMath::Sin(P * UE_TWO_PI); },
		[](float P) { return FMath::Frac(P) < 0.5f ? 0.8f : -0.8f; },
		[](float P) { return 2.f * FMath::Frac(P) - 1.f; },
		[](float P) { return 4.f * FMath::Abs(FMath::Frac(P) - 0.5f) - 1.f; },
	};

	/** Tiny additive synth: tones and filtered noise mixed into a float buffer. */
	struct FSynth
	{
		TArray<float> Buf;
		FRandomStream Rng;

		explicit FSynth(float Seconds, int32 Seed = 7) : Rng(Seed) { Buf.SetNumZeroed(FMath::CeilToInt(Seconds * Rate)); }

		int32 Index(float T) const { return FMath::Clamp(FMath::RoundToInt(T * Rate), 0, Buf.Num()); }

		/** Exponential pitch glide from F0 to F1; amplitude decays by exp(-Decay * t) after a linear attack. */
		void Tone(float Start, float Dur, float F0, float F1, EWave Wave, float Amp, float Decay = 6.f, float Attack = 0.004f, float Vibrato = 0.f)
		{
			const int32 A = Index(Start), B = Index(Start + Dur);
			float Phase = 0.f;
			for (int32 i = A; i < B; ++i)
			{
				const float T = (i - A) / Rate;
				const float U = T / FMath::Max(Dur, 1e-3f);
				float F = F0 * FMath::Pow(F1 / F0, U);
				F *= 1.f + Vibrato * FMath::Sin(T * 34.f);
				Phase += F / Rate;
				const float Env = FMath::Min(T / Attack, 1.f) * FMath::Exp(-Decay * T) * FMath::Min((B - i) / (0.004f * Rate), 1.f);
				Buf[i] += Amp * Env * WaveFns[static_cast<uint8>(Wave)](Phase);
			}
		}

		/** White noise through a one-pole low-pass whose cutoff glides C0 -> C1 (Hz). */
		void Noise(float Start, float Dur, float Amp, float C0, float C1, float Decay = 8.f, float Attack = 0.002f)
		{
			const int32 A = Index(Start), B = Index(Start + Dur);
			float Lp = 0.f;
			for (int32 i = A; i < B; ++i)
			{
				const float T = (i - A) / Rate;
				const float U = T / FMath::Max(Dur, 1e-3f);
				const float Cut = FMath::Lerp(C0, C1, U);
				const float K = 1.f - FMath::Exp(-UE_TWO_PI * Cut / Rate);
				Lp += K * (Rng.FRandRange(-1.f, 1.f) - Lp);
				const float Env = FMath::Min(T / Attack, 1.f) * FMath::Exp(-Decay * T) * FMath::Min((B - i) / (0.004f * Rate), 1.f);
				Buf[i] += Amp * Env * Lp;
			}
		}

		TArray<int16> Finish(float Gain = 1.f) const
		{
			TArray<int16> Out;
			Out.SetNumUninitialized(Buf.Num());
			for (int32 i = 0; i < Buf.Num(); ++i)
			{
				const float S = FMath::Tanh(Buf[i] * Gain); // soft clip: loud and never harsh-wrapping
				Out[i] = static_cast<int16>(FMath::Clamp(S * 32000.f, -32767.f, 32767.f));
			}
			return Out;
		}
	};

	float Note(int32 Semitone) { return 261.63f * FMath::Pow(2.f, Semitone / 12.f); } // 0 = middle C

	TArray<int16> MarchLoop()
	{
		// 120 bpm propaganda march, 8 bars of 4 beats (16 s): oom-pah bass, snare, brass melody.
		const float Beat = 0.5f;
		FSynth S(32 * Beat, 11);
		const int32 Roots[8] = {-24, -24, -19, -19, -22, -22, -24, -17};
		const int32 Melody[32] = {0, 0, 3, 7, 7, 5, 3, 2, 3, 3, 7, 10, 12, 10, 7, 5, 8, 8, 7, 5, 3, 2, 0, -2, 0, 3, 2, -1, 0, 0, -5, 0};
		for (int32 Bar = 0; Bar < 8; ++Bar)
		{
			for (int32 B4 = 0; B4 < 4; ++B4)
			{
				const float T = (Bar * 4 + B4) * Beat;
				const bool bDown = (B4 % 2) == 0;
				const int32 Root = Roots[Bar] + (B4 == 2 ? 7 : 0);
				S.Tone(T, Beat * 0.45f, Note(Root), Note(Root), bDown ? EWave::Tri : EWave::Square, bDown ? 0.45f : 0.0f, 7.f);
				if (!bDown)
				{
					for (int32 C : {12, 15, 19})
					{
						S.Tone(T, Beat * 0.25f, Note(Roots[Bar] + C), Note(Roots[Bar] + C), EWave::Square, 0.07f, 14.f);
					}
					S.Noise(T, 0.18f, 0.35f, 5000.f, 2500.f, 18.f); // snare on 2 and 4
				}
				S.Noise(T, 0.05f, 0.12f, 9000.f, 9000.f, 60.f); // hat
				S.Tone(T, Beat * 0.9f, Note(Melody[Bar * 4 + B4]), Note(Melody[Bar * 4 + B4]), EWave::Saw, 0.12f, 1.5f, 0.02f, 0.006f);
			}
			// Snare roll into every second bar.
			if (Bar % 2 == 1)
			{
				for (int32 R = 0; R < 4; ++R)
				{
					S.Noise((Bar * 4 + 3) * Beat + R * Beat * 0.25f, 0.08f, 0.2f, 5000.f, 3000.f, 30.f);
				}
			}
		}
		return S.Finish(0.9f);
	}

	using FRecipe = TArray<int16> (*)();

	/** The cue registry: name -> recipe. A new sound is a new row. */
	const TMap<FName, FRecipe>& Recipes()
	{
		static const TMap<FName, FRecipe> Map = {
			{TEXT("Shutter"), [] { FSynth S(0.16f); S.Noise(0.f, 0.02f, 0.9f, 9000, 6000, 80.f); S.Noise(0.055f, 0.03f, 0.8f, 7000, 4000, 70.f); S.Tone(0.055f, 0.03f, 1800, 900, EWave::Square, 0.15f, 60.f); return S.Finish(1.4f); }},
			{TEXT("WindOn"), [] { FSynth S(0.4f); for (int32 i = 0; i < 6; ++i) { S.Noise(i * 0.055f, 0.02f, 0.5f, 6000, 3000, 90.f); } return S.Finish(); }},
			{TEXT("Stamp"), [] { FSynth S(0.35f); S.Tone(0.f, 0.25f, 140, 55, EWave::Sine, 1.2f, 14.f); S.Noise(0.f, 0.08f, 0.9f, 3000, 600, 40.f); return S.Finish(1.5f); }},
			{TEXT("Rifle"), [] { FSynth S(0.3f); S.Noise(0.f, 0.25f, 1.2f, 7000, 900, 22.f, 0.0005f); S.Tone(0.f, 0.12f, 160, 60, EWave::Sine, 0.9f, 25.f); return S.Finish(1.6f); }},
			{TEXT("DryFire"), [] { FSynth S(0.06f); S.Noise(0.f, 0.03f, 0.5f, 8000, 5000, 100.f); return S.Finish(); }},
			{TEXT("Reload"), [] { FSynth S(1.3f); S.Noise(0.05f, 0.05f, 0.6f, 4000, 2000, 50.f); S.Tone(0.05f, 0.05f, 500, 300, EWave::Square, 0.12f, 50.f); S.Noise(0.9f, 0.06f, 0.8f, 5000, 2500, 45.f); S.Noise(1.12f, 0.05f, 0.7f, 6000, 3000, 50.f); return S.Finish(); }},
			{TEXT("LowAmmo"), [] { FSynth S(0.12f); S.Tone(0.f, 0.1f, 1400, 1400, EWave::Square, 0.18f, 25.f); return S.Finish(); }},
			{TEXT("HitMarker"), [] { FSynth S(0.06f); S.Tone(0.f, 0.05f, 2400, 2200, EWave::Sine, 0.6f, 60.f); return S.Finish(); }},
			{TEXT("Kill"), [] { FSynth S(0.35f); S.Tone(0.f, 0.12f, 1200, 1200, EWave::Square, 0.25f, 20.f); S.Tone(0.09f, 0.22f, 1800, 1800, EWave::Square, 0.25f, 12.f); return S.Finish(); }},
			{TEXT("Beacon"), [] { FSynth S(0.5f); S.Tone(0.f, 0.45f, 300, 1500, EWave::Square, 0.25f, 3.f); return S.Finish(); }},
			{TEXT("Telegraph"), [] { FSynth S(0.12f); S.Tone(0.f, 0.1f, 990, 990, EWave::Square, 0.35f, 12.f); return S.Finish(); }},
			{TEXT("LaserCharge"), [] { FSynth S(3.2f); S.Tone(0.f, 3.2f, 70, 420, EWave::Saw, 0.35f, -0.35f, 0.3f, 0.02f); S.Noise(0.f, 3.2f, 0.25f, 600, 4000, -0.4f, 0.5f); return S.Finish(); }},
			{TEXT("Detonation"), [] { FSynth S(3.5f, 3); S.Noise(0.f, 3.5f, 2.5f, 9000, 120, 1.6f, 0.0005f); S.Tone(0.f, 2.5f, 70, 28, EWave::Sine, 2.f, 1.6f); S.Noise(0.f, 0.12f, 1.5f, 12000, 6000, 30.f); return S.Finish(2.f); }},
			{TEXT("Collapse"), [] { FSynth S(3.5f, 5); S.Noise(0.f, 3.5f, 1.6f, 900, 200, 0.9f, 0.15f); for (int32 i = 0; i < 18; ++i) { S.Noise(S.Rng.FRandRange(0.f, 2.8f), 0.08f, 0.7f, 3000, 800, 30.f); } return S.Finish(1.6f); }},
			{TEXT("Footstep"), [] { FSynth S(0.09f); S.Noise(0.f, 0.08f, 0.5f, 1400, 400, 40.f); return S.Finish(); }},
			{TEXT("Land"), [] { FSynth S(0.2f); S.Noise(0.f, 0.18f, 0.9f, 1000, 200, 18.f); S.Tone(0.f, 0.12f, 110, 60, EWave::Sine, 0.6f, 20.f); return S.Finish(); }},
			{TEXT("Hurt"), [] { FSynth S(0.3f); S.Tone(0.f, 0.25f, 220, 140, EWave::Saw, 0.4f, 6.f, 0.01f, 0.04f); S.Noise(0.f, 0.06f, 0.5f, 3000, 1000, 40.f); return S.Finish(); }},
			{TEXT("UIClick"), [] { FSynth S(0.06f); S.Tone(0.f, 0.05f, 1000, 1000, EWave::Square, 0.2f, 40.f); return S.Finish(); }},
			{TEXT("UIConfirm"), [] { FSynth S(0.3f); S.Tone(0.f, 0.1f, 660, 660, EWave::Square, 0.2f, 15.f); S.Tone(0.1f, 0.18f, 990, 990, EWave::Square, 0.2f, 10.f); return S.Finish(); }},
			{TEXT("Swarmer"), [] { FSynth S(0.5f); S.Tone(0.f, 0.5f, 210, 260, EWave::Saw, 0.3f, 1.f, 0.05f, 0.1f); return S.Finish(); }},
			{TEXT("SwarmerAggro"), [] { FSynth S(0.4f); S.Tone(0.f, 0.35f, 400, 900, EWave::Saw, 0.35f, 2.f, 0.01f, 0.08f); return S.Finish(); }},
			{TEXT("Spotlight"), [] { FSynth S(0.8f); S.Tone(0.f, 0.8f, 120, 120, EWave::Square, 0.2f, 0.5f, 0.1f, 0.02f); S.Tone(0.f, 0.8f, 2400, 2600, EWave::Sine, 0.08f, 1.f, 0.1f); return S.Finish(); }},
			{TEXT("WalkerStomp"), [] { FSynth S(0.6f); S.Tone(0.f, 0.5f, 70, 35, EWave::Sine, 1.4f, 7.f); S.Noise(0.f, 0.3f, 0.8f, 800, 200, 12.f); return S.Finish(1.5f); }},
			{TEXT("Scoop"), [] { FSynth S(1.0f); for (int32 i = 0; i < 3; ++i) { S.Tone(i * 0.14f, 0.13f, Note(7 + i * 5), Note(7 + i * 5), EWave::Square, 0.25f, 6.f); } S.Tone(0.42f, 0.5f, Note(19), Note(19), EWave::Saw, 0.25f, 3.f, 0.01f, 0.01f); return S.Finish(); }},
			{TEXT("PodDrop"), [] { FSynth S(1.6f); S.Noise(0.f, 1.2f, 0.6f, 500, 6000, -1.f, 0.8f); S.Tone(1.2f, 0.35f, 120, 50, EWave::Sine, 1.2f, 10.f); S.Noise(1.2f, 0.3f, 0.9f, 2000, 300, 12.f); return S.Finish(1.4f); }},
			{TEXT("Pickup"), [] { FSynth S(0.3f); S.Tone(0.f, 0.25f, 600, 1400, EWave::Square, 0.2f, 6.f); return S.Finish(); }},
			{TEXT("Whoosh"), [] { FSynth S(0.9f); S.Noise(0.f, 0.9f, 0.9f, 300, 5000, 2.f, 0.3f); return S.Finish(); }},
			{TEXT("Flash"), [] { FSynth S(0.5f); S.Tone(0.f, 0.45f, 3000, 6000, EWave::Sine, 0.2f, 4.f); S.Noise(0.f, 0.05f, 0.7f, 10000, 8000, 40.f); return S.Finish(); }},
			{TEXT("Press"), [] { FSynth S(1.6f); for (int32 i = 0; i < 4; ++i) { S.Noise(i * 0.35f, 0.2f, 1.f, 1500, 300, 15.f); S.Tone(i * 0.35f, 0.15f, 90, 60, EWave::Sine, 0.8f, 15.f); } return S.Finish(1.3f); }},
			{TEXT("Draw"), [] { FSynth S(0.3f); S.Noise(0.f, 0.25f, 0.5f, 1500, 5000, 6.f, 0.05f); S.Noise(0.22f, 0.04f, 0.6f, 6000, 3000, 60.f); return S.Finish(); }},
			{TEXT("Jump"), [] { FSynth S(0.18f); S.Noise(0.f, 0.15f, 0.6f, 1600, 500, 25.f); S.Tone(0.f, 0.08f, 140, 90, EWave::Sine, 0.4f, 30.f); return S.Finish(); }},
			{TEXT("Creak"), [] { FSynth S(1.6f, 7); S.Tone(0.f, 1.5f, 55, 38, EWave::Saw, 0.6f, 1.2f, 0.2f, 0.08f); for (int32 i = 0; i < 6; ++i) { S.Noise(S.Rng.FRandRange(0.1f, 1.4f), 0.12f, 0.6f, 900, 300, 20.f); } return S.Finish(1.3f); }},
			{TEXT("Down"), [] { FSynth S(1.2f); S.Tone(0.f, 1.1f, 400, 80, EWave::Saw, 0.4f, 1.5f); return S.Finish(); }},
			{TEXT("Revive"), [] { FSynth S(0.7f); for (int32 i = 0; i < 4; ++i) { S.Tone(i * 0.12f, 0.2f, Note(i * 4), Note(i * 4), EWave::Tri, 0.35f, 6.f); } return S.Finish(); }},
			{TEXT("Music"), &MarchLoop},
		};
		return Map;
	}

	TArray<uint8> ToBytes(const TArray<int16>& Samples)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Samples.Num() * sizeof(int16));
		FMemory::Memcpy(Bytes.GetData(), Samples.GetData(), Bytes.Num());
		return Bytes;
	}
}

namespace DCAudio
{
	TArray<FName> CueNames()
	{
		TArray<FName> Names;
		Recipes().GetKeys(Names);
		return Names;
	}

	TArray<int16> Render(FName Cue)
	{
		const FRecipe* Recipe = Recipes().Find(Cue);
		return Recipe ? (*Recipe)() : TArray<int16>();
	}

	static UDCAudioSubsystem* Get(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UDCAudioSubsystem>() : nullptr;
	}

	void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch)
	{
		if (UDCAudioSubsystem* Audio = Get(WorldContext))
		{
			Audio->PlayAt(WorldContext, Cue, Location, Volume, Pitch);
		}
	}

	void Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch)
	{
		if (UDCAudioSubsystem* Audio = Get(WorldContext))
		{
			Audio->Play2D(WorldContext, Cue, Volume, Pitch);
		}
	}
}

void UDCAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	for (const TPair<FName, FRecipe>& Pair : Recipes())
	{
		Bank.Add(Pair.Key, MakeShared<TArray<uint8>>(ToBytes(Pair.Value())));
	}
	Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector(1500.f, 0.f, 0.f);
	Attenuation->Attenuation.FalloffDistance = 14000.f;
}

float UDCAudioSubsystem::CueSeconds(FName Cue) const
{
	const TSharedPtr<TArray<uint8>>* Data = Bank.Find(Cue);
	return Data ? (*Data)->Num() / (2.f * SampleRate) : 0.f;
}

USoundWaveProcedural* UDCAudioSubsystem::MakeWave(FName Cue) const
{
	const TSharedPtr<TArray<uint8>>* Data = Bank.Find(Cue);
	if (!Data)
	{
		return nullptr;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = (*Data)->Num() / (2.f * SampleRate);
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio((*Data)->GetData(), (*Data)->Num());
	return Wave;
}

void UDCAudioSubsystem::PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch)
{
	if (USoundWaveProcedural* Wave = MakeWave(Cue))
	{
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Wave, Location, Volume, Pitch, 0.f, Attenuation);
	}
}

void UDCAudioSubsystem::Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch)
{
	if (USoundWaveProcedural* Wave = MakeWave(Cue))
	{
		UGameplayStatics::PlaySound2D(WorldContext, Wave, Volume, Pitch);
	}
}

void UDCAudioSubsystem::StartMusic(const UObject* WorldContext, FName Cue, float Volume)
{
	if (Music && Music->IsPlaying() && MusicCue == Cue)
	{
		Music->SetVolumeMultiplier(Volume);
		return;
	}
	const TSharedPtr<TArray<uint8>>* Data = Bank.Find(Cue);
	if (!Data)
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->bLooping = true;
	TSharedPtr<TArray<uint8>> Loop = *Data;
	Wave->OnSoundWaveProceduralUnderflow.BindLambda([Loop](USoundWaveProcedural* W, int32 /*SamplesRequired*/)
	{
		W->QueueAudio(Loop->GetData(), Loop->Num());
	});
	Wave->QueueAudio(Loop->GetData(), Loop->Num());
	if (Music)
	{
		Music->Stop();
	}
	Music = UGameplayStatics::CreateSound2D(WorldContext, Wave, Volume, 1.f, 0.f, nullptr, true, false);
	if (Music)
	{
		Music->Play();
	}
	MusicCue = Cue;
}

void UDCAudioSubsystem::SetMusicVolume(float Volume)
{
	if (Music)
	{
		Music->SetVolumeMultiplier(Volume);
	}
}
