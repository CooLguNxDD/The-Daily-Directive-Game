#include "DCAudio.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "Misc/App.h"
#include "DCFrameScopes.h"
#include "DCSave.h"
#include "DCTuning.h"
#include "Rules/DCCueMix.h"

#include "AudioMixerBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
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
			{TEXT("FactionShot"), [] { FSynth S(0.18f); S.Noise(0.f, 0.12f, 0.8f, 5000, 1200, 30.f, 0.001f); S.Tone(0.f, 0.08f, 900, 300, EWave::Square, 0.2f, 30.f); return S.Finish(1.2f); }},
			{TEXT("Groan"), [] { FSynth S(0.9f); S.Tone(0.f, 0.85f, 95, 70, EWave::Saw, 0.35f, 1.5f, 0.15f, 0.06f); S.Noise(0.f, 0.8f, 0.2f, 700, 300, 2.f); return S.Finish(); }},
			{TEXT("FactionStrike"), [] { FSynth S(1.2f); for (int32 i = 0; i < 3; ++i) { S.Tone(i * 0.3f, 0.22f, 520, 520, EWave::Square, 0.3f, 6.f); } return S.Finish(); }},
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
			{TEXT("March"), &MarchLoop}, // R14 Fight layer (human 15:25): a +MusicTracks row names it with Synth="March"
		};
		return Map;
	}

	/** A data recipe (+SoundRecipes) through the same synth: layers dispatched by wave name, no branches per kind. */
	TArray<int16> RenderData(const FDCSoundRecipe& Recipe)
	{
		FSynth S(FMath::Clamp(Recipe.Seconds, 0.01f, 20.f), Recipe.Seed);
		using FLayerFn = void (*)(FSynth&, const FDCSoundLayer&, float Start);
		static const TMap<FName, FLayerFn> Layers = {
			{TEXT("Noise"), [](FSynth& Sy, const FDCSoundLayer& L, float At) { Sy.Noise(At, L.Dur, L.Amp, L.F0, L.F1, L.Decay, L.Attack); }},
			{TEXT("Sine"), [](FSynth& Sy, const FDCSoundLayer& L, float At) { Sy.Tone(At, L.Dur, L.F0, L.F1, EWave::Sine, L.Amp, L.Decay, L.Attack, L.Vibrato); }},
			{TEXT("Square"), [](FSynth& Sy, const FDCSoundLayer& L, float At) { Sy.Tone(At, L.Dur, L.F0, L.F1, EWave::Square, L.Amp, L.Decay, L.Attack, L.Vibrato); }},
			{TEXT("Saw"), [](FSynth& Sy, const FDCSoundLayer& L, float At) { Sy.Tone(At, L.Dur, L.F0, L.F1, EWave::Saw, L.Amp, L.Decay, L.Attack, L.Vibrato); }},
			{TEXT("Tri"), [](FSynth& Sy, const FDCSoundLayer& L, float At) { Sy.Tone(At, L.Dur, L.F0, L.F1, EWave::Tri, L.Amp, L.Decay, L.Attack, L.Vibrato); }},
		};
		for (const FDCSoundLayer& L : Recipe.Layers)
		{
			const FLayerFn* Fn = Layers.Find(L.Wave);
			if (!Fn || L.F0 <= 0.f || L.F1 <= 0.f)
			{
				continue; // untrusted data: an unknown wave or a zero frequency is skipped, never a crash
			}
			for (int32 r = 0; r < FMath::Clamp(L.Repeat, 1, 64); ++r)
			{
				(*Fn)(S, L, L.Start + r * L.RepeatGap);
			}
		}
		return S.Finish(Recipe.Gain);
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
		for (const FDCSoundRecipe& Data : UDCTuning::Get().SoundRecipes)
		{
			Names.AddUnique(Data.Id);
		}
		return Names;
	}

	TArray<int16> Render(FName Cue)
	{
		if (const FRecipe* Recipe = Recipes().Find(Cue))
		{
			return (*Recipe)();
		}
		const FDCSoundRecipe* Data = UDCTuning::Get().FindSoundRecipe(Cue);
		return Data ? RenderData(*Data) : TArray<int16>();
	}

	USoundWaveProcedural* MakeLoop(UObject* Outer, FName Recipe)
	{
		const TArray<int16> Samples = Render(Recipe);
		if (Samples.Num() == 0)
		{
			return nullptr;
		}
		TSharedPtr<TArray<uint8>> Loop = MakeShared<TArray<uint8>>();
		Loop->SetNumUninitialized(Samples.Num() * sizeof(int16));
		FMemory::Memcpy(Loop->GetData(), Samples.GetData(), Loop->Num());
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Outer);
		Wave->SetSampleRate(UDCAudioSubsystem::SampleRate);
		Wave->NumChannels = 1;
		Wave->Duration = Samples.Num() / static_cast<float>(UDCAudioSubsystem::SampleRate); // one bar cycle, for playlists
		Wave->bLooping = true;
		Wave->OnSoundWaveProceduralUnderflow.BindLambda([Loop](USoundWaveProcedural* W, int32 /*SamplesRequired*/)
		{
			W->QueueAudio(Loop->GetData(), Loop->Num()); // the full 16 s again: seamless (it renders as a whole loop)
		});
		Wave->QueueAudio(Loop->GetData(), Loop->Num());
		return Wave;
	}

	static UDCAudioSubsystem* Get(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UDCAudioSubsystem>() : nullptr;
	}

	/** The saved master volume (R24), applied to every cue. */
	float Master(const UObject* WorldContext)
	{
		const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(WorldContext);
		return Save ? FMath::Clamp(Save->Settings.MasterVolume, 0.f, 1.f) : 1.f;
	}

	void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch)
	{
		if (UDCAudioSubsystem* Audio = Get(WorldContext))
		{
			Audio->PlayAt(WorldContext, Cue, Location, Volume * Master(WorldContext), Pitch);
		}
	}

	void Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch)
	{
		if (UDCAudioSubsystem* Audio = Get(WorldContext))
		{
			Audio->Play2D(WorldContext, Cue, Volume * Master(WorldContext), Pitch);
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
	for (const FDCSoundRecipe& Data : UDCTuning::Get().SoundRecipes)
	{
		if (!Bank.Contains(Data.Id)) // a code cue of the same name wins
		{
			Bank.Add(Data.Id, MakeShared<TArray<uint8>>(ToBytes(RenderData(Data))));
		}
	}
	Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector(1500.f, 0.f, 0.f);
	Attenuation->Attenuation.FalloffDistance = 14000.f;
	for (const FDCCueMix& Mix : UDCTuning::Get().CueMix)
	{
		USoundBase* Remix = Mix.Sound.IsNull() ? nullptr : Cast<USoundBase>(Mix.Sound.TryLoad());
		if (Remix)
		{
			Remix->bBypassVolumeScaleForPriority = true; // a far strike must not lose its voice to a near click (a-40)
			MixSounds.Add(Mix.Cue, Remix);
		}
		if (!Mix.Sound.IsNull())
		{
			// R13 trace (human 02:38: no detonation in play): which remix recordings the packaged build actually has.
			UE_LOG(LogTemp, Display, TEXT("DCAUDIO remix %s -> %s loaded=%d duration=%.2f"), *Mix.Cue.ToString(), *Mix.Sound.ToString(), Remix ? 1 : 0, Remix ? Remix->GetDuration() : 0.f);
		}
	}
	FlushHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDCAudioSubsystem::FlushQueue));
}

void UDCAudioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(FlushHandle);
	Queue.Reset();
	Super::Deinitialize();
}

bool UDCAudioSubsystem::FlushQueue(float DeltaSeconds)
{
	DC_FRAME_SCOPE("SoundQueue");
	const double NowReal = FPlatformTime::Seconds();
	for (int32 i = LiveShots.Num() - 1; i >= 0; --i)
	{
		UAudioComponent* C = LiveShots[i].Key.Get();
		if (C && NowReal < LiveShots[i].Value)
		{
			continue;
		}
		if (C)
		{
			C->Stop();
		}
		LiveShots.RemoveAtSwap(i, EAllowShrinking::No);
	}
	if (RecordStart >= 0.0 && NowReal >= NextVoicesLog)
	{
		NextVoicesLog = NowReal + 1.0;
		const FAudioDevice* Device = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("DCAUDIO voices t=%.1f active_sources=%d max_channels=%d synth_shots=%d"), NowReal - RecordStart,
			Device ? Device->GetNumActiveSources() : -1, Device ? Device->GetMaxChannels() : -1, LiveShots.Num());
	}
	if (Queue.Num() == 0)
	{
		return true;
	}
	const UDCTuning& T = UDCTuning::Get();
	TArray<FDCSoundRequest> Plays;
	TArray<FDCSoundDropped> Drops;
	DCSoundQueue::Flush(T.SoundClasses, T.SoundMix, FPlatformTime::Seconds(), [this](FName Cue) { return CueSeconds(Cue); }, Queue, Voices, Plays, Drops);
	for (const FDCSoundRequest& R : Plays)
	{
		Start(R);
	}
	for (const FDCSoundDropped& D : Drops)
	{
		const bool bMerged = D.Why == EDCSoundDrop::Merged;
		(bMerged ? MergedCount : Dropped) += 1;
		if (RecordStart >= 0.0 || !bMerged)
		{
			UE_LOG(LogTemp, Display, TEXT("DCAUDIO %s %s after %d frames (dropped %d, merged %d so far)"), bMerged ? TEXT("merged") : TEXT("drop"), *D.Request.Cue.ToString(), D.Request.WaitedFrames, Dropped, MergedCount);
		}
	}
	return true;
}

void UDCAudioSubsystem::Start(const FDCSoundRequest& R)
{
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const UDCTuning& T = UDCTuning::Get();
	const TArray<FDCSoundClassDef>& Classes = T.SoundClasses;
	const int32 Ci = DCSoundQueue::ClassOf(Classes, R.Cue);
	const int32 Priority = Classes.IsValidIndex(Ci) ? Classes[Ci].Priority : 50;
	LogCue(World, R.Cue, R.b2D ? nullptr : &R.Location);
	// Mixer pass: the cue's own trim (+CueMix), then ducking under a recent strike for lower-priority classes.
	const double Real = FPlatformTime::Seconds();
	float Volume = R.Volume * DCCueMix::Trim(T.CueMix, R.Cue);
	Volume *= Real < DuckUntil && Priority < DuckPriority ? DuckScale : 1.f;
	if (Classes.IsValidIndex(Ci) && Classes[Ci].DuckOthers > 0.f)
	{
		DuckUntil = Real + Classes[Ci].DuckSeconds;
		DuckPriority = Priority;
		DuckScale = FMath::Clamp(1.f - Classes[Ci].DuckOthers, 0.f, 1.f);
	}
	// A remixed recording plays instead of the synth wave (a normal sound wave: no pool needed).
	USoundBase* Sound = MixSounds.FindRef(R.Cue);
	USoundWaveProcedural* Wave = Sound ? nullptr : AcquireWave(R.Cue, R.Pitch, Priority);
	Sound = Sound ? Sound : Wave;
	if (Sound && Sound != Wave)
	{
		// The remix asset kept the engine default priority 1.0, under every synth wave (class Priority / 25: units 1.6,
		// strikes 4): past AudioMaxChannels the engine culled the remixed detonation first (R13, human 02:38).
		Sound->Priority = static_cast<float>(Priority) / 25.f;
	}
	if (!Sound)
	{
		UE_LOG(LogTemp, Display, TEXT("DCAUDIO drop %s: wave pool full"), *R.Cue.ToString());
		return;
	}
	if (RecordStart >= 0.0 || Priority >= 100)
	{
		// R13 trace: the sound that actually starts for a cue (remix or synth), its final volume, ducking and 2D/3D.
		UE_LOG(LogTemp, Display, TEXT("DCAUDIO start %s via=%s vol=%.2f pitch=%.2f 2d=%d ducked=%d"), *R.Cue.ToString(), *Sound->GetName(), Volume, R.Pitch, R.b2D ? 1 : 0,
			Real < DuckUntil && Priority < DuckPriority ? 1 : 0);
	}
	// R13/R14 cutout (advisor a-40, audio-8f17486): a generator-less procedural source never reports finished, so every
	// synth one-shot held a voice for good; past AudioMaxChannels (~37 s into a fight) the engine dropped the music and the
	// remixed boom. Synth plays get a component that is stopped when its PCM ends, which frees the voice before reuse.
	UAudioComponent* Shot = R.b2D ? UGameplayStatics::SpawnSound2D(World, Sound, Volume, R.Pitch)
		: UGameplayStatics::SpawnSoundAtLocation(World, Sound, R.Location, FRotator::ZeroRotator, Volume, R.Pitch, 0.f, Attenuation);
	if (Shot && Wave)
	{
		LiveShots.Add({Shot, FPlatformTime::Seconds() + CueSeconds(R.Cue) / FMath::Max(R.Pitch, 0.1f) + 0.05});
	}
}

USoundWaveProcedural* UDCAudioSubsystem::AcquireWave(FName Cue, float Pitch, int32 Priority)
{
	// One small pool of procedural waves per cue: a procedural wave plays one source at a time, so a wave is refilled
	// and reused only after its last play ended (plus a guard); no NewObject per play once the pool is warm.
	const TSharedPtr<TArray<uint8>>* Data = Bank.Find(Cue);
	if (!Data)
	{
		return nullptr;
	}
	const FDCSoundMixTuning& Mix = UDCTuning::Get().SoundMix;
	const double Now = FPlatformTime::Seconds();
	TArray<int32>& Pool = PoolOf.FindOrAdd(Cue);
	int32 Pick = INDEX_NONE;
	for (const int32 i : Pool)
	{
		Pick = Pick == INDEX_NONE && PoolFreeAt[i] <= Now ? i : Pick;
	}
	if (Pick == INDEX_NONE)
	{
		if (Pool.Num() >= FMath::Max(1, Mix.PoolPerCue))
		{
			return nullptr;
		}
		Pick = PoolWaves.Add(MakeWave(Cue));
		PoolFreeAt.Add(0.0);
		Pool.Add(Pick);
	}
	else
	{
		PoolWaves[Pick]->ResetAudio();
		PoolWaves[Pick]->QueueAudio((*Data)->GetData(), (*Data)->Num());
	}
	USoundWaveProcedural* Wave = PoolWaves[Pick];
	Wave->Priority = static_cast<float>(Priority) / 25.f; // the engine culls lower priority first past AudioMaxChannels
	PoolFreeAt[Pick] = Now + CueSeconds(Cue) / FMath::Max(Pitch, 0.1f) + Mix.ReuseGuardSeconds;
	return Wave;
}

float UDCAudioSubsystem::CueSeconds(FName Cue) const
{
	// A remixed recording is what plays: its own length holds the class voice and the merge window (review F25).
	if (const USoundBase* Remix = MixSounds.FindRef(Cue))
	{
		const float D = Remix->GetDuration();
		if (D > 0.f && !Remix->IsLooping())
		{
			return D;
		}
	}
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

void UDCAudioSubsystem::StartEvidenceRecording(const UObject* WorldContext)
{
	if (RecordStart >= 0.0 || !FParse::Value(FCommandLine::Get(), TEXT("dcaudiorec="), RecordDir))
	{
		return;
	}
	// Evidence runs share the desktop with other windows: an unfocused game is muted by the engine (audio-6f4334f had
	// 44 s stretches of digital silence). The recording hears the game as a focused player would.
	FApp::SetUnfocusedVolumeMultiplier(1.f);
	UAudioMixerBlueprintLibrary::StartRecordingOutput(WorldContext, 1800.f);
	RecordStart = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("DCCUE recording started to %s"), *RecordDir);
	// -dcaudiorecseconds=N: stop and write the mix after N real seconds (a job's timeout may end the run first).
	float Seconds = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("dcaudiorecseconds="), Seconds) && Seconds > 0.f)
	{
		TWeakObjectPtr<const UObject> Context(WorldContext);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Context](float)
		{
			if (Context.IsValid())
			{
				StopEvidenceRecording(Context.Get());
			}
			return false;
		}), Seconds);
	}
}

void UDCAudioSubsystem::StopEvidenceRecording(const UObject* WorldContext)
{
	if (RecordStart < 0.0)
	{
		return;
	}
	UAudioMixerBlueprintLibrary::StopRecordingOutputToAnyPath(WorldContext, EAudioRecordingExportType::WavFile, TEXT("mix"), RecordDir);
	UE_LOG(LogTemp, Display, TEXT("DCCUE recording stopped t=%.3f"), FPlatformTime::Seconds() - RecordStart);
	RecordStart = -1.0;
}

void UDCAudioSubsystem::LogCue(const UObject* WorldContext, FName Cue, const FVector* Location) const
{
	if (RecordStart < 0.0)
	{
		return;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const float Dist = Location && PC && PC->PlayerCameraManager ? FVector::Dist(*Location, PC->PlayerCameraManager->GetCameraLocation()) : 0.f;
	UE_LOG(LogTemp, Display, TEXT("DCCUE %s t=%.3f dist=%.0f"), *Cue.ToString(), FPlatformTime::Seconds() - RecordStart, Dist);
}

void UDCAudioSubsystem::PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch)
{
	// R13: queued; FlushQueue merges, budgets and starts it this frame or a few frames later.
	if (Bank.Contains(Cue))
	{
		Queue.Add({Cue, Location, false, Volume, Pitch});
	}
}

void UDCAudioSubsystem::Play2D(const UObject* WorldContext, FName Cue, float Volume, float Pitch)
{
	if (Bank.Contains(Cue))
	{
		Queue.Add({Cue, FVector::ZeroVector, true, Volume, Pitch});
	}
}

void UDCAudioSubsystem::StartMusic(const UObject* WorldContext, FName Cue, float Volume)
{
	MusicBase = Volume;
	Volume *= DCAudio::Master(WorldContext);
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
	MusicBase = Volume;
	if (Music)
	{
		Music->SetVolumeMultiplier(Volume * DCAudio::Master(Music));
	}
}

void UDCAudioSubsystem::ApplyMasterVolume(const UObject* WorldContext)
{
	if (Music)
	{
		Music->SetVolumeMultiplier(MusicBase * DCAudio::Master(WorldContext));
	}
}
