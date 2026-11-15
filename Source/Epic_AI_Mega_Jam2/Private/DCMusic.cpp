#include "DCMusic.h"
#include "DCFrameScopes.h"
#include "DCAudio.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "DCUnits.h"
#include "Rules/DCFactionRules.h"
#include "Rules/DCMusicRules.h"

#include "Algo/Transform.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace
{
	/** A layered sibling's level while another layer plays: inaudible (-80 dB) but above the engine's "zero means stop"
	 *  rule for AdjustVolume, so it keeps playing in time. */
	constexpr float SilentLevel = 1e-4f;
}

UDCMusicSubsystem* UDCMusicSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDCMusicSubsystem>() : nullptr;
}

void UDCMusicSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& D : Decks)
	{
		if (D.Value)
		{
			D.Value->Stop();
		}
	}
	for (UAudioComponent* C : Tails)
	{
		if (C)
		{
			C->Stop();
		}
	}
	Decks.Reset();
	Tails.Reset();
	Super::Deinitialize();
}

bool UDCMusicSubsystem::Start(const UObject* WorldContext)
{
	if (TickHandle.IsValid())
	{
		return true;
	}
	const UDCTuning& T = UDCTuning::Get();
	for (const FDCMusicTrack& Track : T.MusicTracks)
	{
		USoundBase* Sound = Cast<USoundBase>(Track.Sound.TryLoad());
		if (!Sound)
		{
			UE_LOG(LogTemp, Warning, TEXT("DCMUSIC track %s: no sound at %s"), *Track.Id.ToString(), *Track.Sound.ToString());
			continue;
		}
		// Silent layers keep running (in time) and the score outranks any SFX flood for a voice.
		Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		Sound->Priority = T.Music.Priority;
		Sounds.Add(Track.Id, Sound);
	}
	if (Sounds.Num() == 0)
	{
		return false;
	}
	const float Interval = 1.f / FMath::Clamp(T.Music.SampleHz, 0.5f, 30.f);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDCMusicSubsystem::Tick), Interval);
	UE_LOG(LogTemp, Display, TEXT("DCMUSIC started: %d tracks, sampling at %.1f Hz"), Sounds.Num(), 1.f / Interval);
	return true;
}

void UDCMusicSubsystem::SetDuck(float InDuck)
{
	Duck = FMath::Clamp(InDuck, 0.f, 1.f);
	ApplyLevels();
}

bool UDCMusicSubsystem::Tick(float DeltaSeconds)
{
	DC_FRAME_SCOPE("Music");
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
	if (GS)
	{
		const double NowReal = FPlatformTime::Seconds();
		const float Since = LastSampleReal > 0.0 ? static_cast<float>(NowReal - LastSampleReal) : DeltaSeconds;
		LastSampleReal = NowReal;
		Sample(*World, *GS, DCMusicRules::StepSeconds(UDCTuning::Get().Music, Since));
	}
	return true;
}

float UDCMusicSubsystem::ReadIntensity(UWorld& World, const ADCGameState& GS, int32& OutHostiles, int32& OutBlasts) const
{
	OutHostiles = 0;
	OutBlasts = 0;
	const APlayerController* PC = World.GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return 0.f;
	}
	const UDCTuning& T = UDCTuning::Get();
	const FDCMusicTuning& M = T.Music;
	const FVector View = PC->PlayerCameraManager->GetCameraLocation();
	if (const UDCUnits* Units = UDCUnits::Get(&World))
	{
		const float R2 = FMath::Square(M.HostileRadiusCm);
		for (const ADCEnemy* U : Units->All())
		{
			const bool bHostile = U && U->IsAlive() && (U->GetFaction().IsNone() || DCFactionRules::FightsCrew(T.Factions, U->GetFaction()));
			OutHostiles += bHostile && FVector::DistSquared(U->GetActorLocation(), View) <= R2 ? 1 : 0;
		}
	}
	TArray<FDCMusicBlast, TInlineAllocator<16>> Blasts;
	const float Now = GS.Now();
	for (const FDCBlastRecord& B : GS.Blasts)
	{
		const float Age = Now - B.Time;
		if (Age <= M.BlastHoldSeconds && Now >= B.TelegraphStart)
		{
			Blasts.Add({Age, static_cast<float>(FVector::Dist(B.Location, View)), Now - B.TelegraphStart});
		}
	}
	OutBlasts = Blasts.Num();
	return DCMusicRules::Intensity(M, OutHostiles, Blasts);
}

FName UDCMusicSubsystem::PickTrack(const FDCMusicPhase& P, float Dt, float& InOutFade)
{
	const UDCTuning& T = UDCTuning::Get();
	if (P.bByIntensity)
	{
		const int32 Next = DCMusicRules::PickLayer(T.MusicLayers, Layer, Intensity, LayerHeld);
		LayerHeld += Dt;
		if (Next != Layer && T.MusicLayers.IsValidIndex(Next))
		{
			InOutFade = T.MusicLayers.IsValidIndex(Layer) ? T.MusicLayers[Next].FadeInSeconds : InOutFade;
			Layer = Next;
			LayerHeld = 0.f;
		}
		return T.MusicLayers.IsValidIndex(Layer) ? T.MusicLayers[Layer].Track : NAME_None;
	}
	if (P.Playlist.Num() == 0)
	{
		return NAME_None;
	}
	Slot = Slot % P.Playlist.Num();
	SlotElapsed += Dt;
	const float Len = T.Music.PlaylistSeconds > 0.f ? T.Music.PlaylistSeconds : TrackSeconds(P.Playlist[Slot]);
	if (P.Playlist.Num() > 1 && Len > 0.f && SlotElapsed >= Len - T.Music.PlaylistFadeSeconds)
	{
		Slot = DCMusicRules::NextSlot(Slot, P.Playlist.Num());
		SlotElapsed = 0.f;
		InOutFade = T.Music.PlaylistFadeSeconds;
	}
	return P.Playlist[Slot];
}

void UDCMusicSubsystem::Sample(UWorld& World, const ADCGameState& GS, float Dt)
{
	const UDCTuning& T = UDCTuning::Get();
	int32 Hostiles = 0, NumBlasts = 0;
	Raw = ReadIntensity(World, GS, Hostiles, NumBlasts);
	Intensity = DCMusicRules::Smooth(T.Music, Intensity, Raw, Dt);
	if (!FMath::IsNearlyEqual(AppliedMaster, DCAudio::Master(&World)))
	{
		ApplyLevels(); // the saved master volume changed (settings, R24)
	}
	const FDCMusicPhase* P = T.FindMusicPhase(GS.Phase);
	if (!P)
	{
		return; // an unlisted phase keeps whatever plays
	}
	float Fade = P->FadeInSeconds;
	if (GS.Phase != Phase)
	{
		Phase = GS.Phase;
		Layer = INDEX_NONE;
		LayerHeld = 0.f;
		SlotElapsed = 0.f;
	}
	const FName Want = PickTrack(*P, Dt, Fade);
	// Self-heal (R14, human 02:39 "music does not blend"): a deck the engine stopped (focus loss, device change, a culled
	// voice) never came back, because SetTarget returns early while the wanted track is still "Playing". Restart it.
	const TObjectPtr<UAudioComponent>* Live = Playing.IsNone() ? nullptr : Decks.Find(Playing);
	if (!Playing.IsNone() && (!Live || !*Live || !(*Live)->IsPlaying()))
	{
		UE_LOG(LogTemp, Display, TEXT("DCMUSIC deck %s was stopped by the engine: restarting"), *Playing.ToString());
		Playing = NAME_None;
	}
	TArray<FName> Keep;
	if (P->bByIntensity)
	{
		Algo::Transform(T.MusicLayers, Keep, &FDCMusicLayer::Track);
	}
	const FName Was = Playing;
	SetTarget(World, Want, Fade, Keep);
	const double Real = World.GetRealTimeSeconds();
	const bool bSwitched = Was != Playing;
	const bool bMoved = Hostiles != LastHostiles || NumBlasts != LastBlasts || Raw > 0.01f || Intensity > 0.01f;
	if (bSwitched || (T.Music.LogEverySeconds > 0.f && Real >= NextLog && bMoved))
	{
		NextLog = Real + T.Music.LogEverySeconds;
		UE_LOG(LogTemp, Display, TEXT("DCMUSIC t=%.2f phase=%s hostiles=%d blasts=%d raw=%.2f intensity=%.2f layer=%s track=%s%s"),
			Real, *UEnum::GetValueAsString(GS.Phase), Hostiles, NumBlasts, Raw, Intensity,
			T.MusicLayers.IsValidIndex(Layer) && P->bByIntensity ? *T.MusicLayers[Layer].Id.ToString() : TEXT("-"),
			*Playing.ToString(), bSwitched ? *FString::Printf(TEXT(" switch from=%s fade=%.1f"), *Was.ToString(), Fade) : TEXT(""));
	}
	LastHostiles = Hostiles;
	LastBlasts = NumBlasts;
}

void UDCMusicSubsystem::SetTarget(UWorld& World, FName Track, float Fade, const TArray<FName>& Keep)
{
	if (Track == Playing)
	{
		return;
	}
	Playing = Track;
	const float F = FMath::Max(Fade, 0.f);
	// Start the decks this target needs (the wanted track and, in a layered phase, its silent siblings).
	TArray<FName> Need = Keep;
	if (!Track.IsNone())
	{
		Need.AddUnique(Track);
	}
	Tails.RemoveAll([](const TObjectPtr<UAudioComponent>& C) { return !C || !C->IsPlaying(); });
	TArray<FName, TInlineAllocator<4>> Started;
	for (const FName& Id : Need)
	{
		const TObjectPtr<UAudioComponent>* Found = Decks.Find(Id);
		USoundBase* Sound = Sounds.FindRef(Id);
		if ((Found && *Found && (*Found)->IsPlaying() && (*Found)->GetPlayState() != EAudioComponentPlayState::FadingOut) || !Sound)
		{
			continue;
		}
		if (Found && *Found && (*Found)->IsPlaying())
		{
			Tails.Add(*Found); // a fade-out cannot be taken back: let it finish, start the track afresh
		}
		UAudioComponent* Deck = UGameplayStatics::CreateSound2D(&World, Sound, TrackLevel(Id), 1.f, 0.f, nullptr, true, false);
		if (!Deck)
		{
			continue;
		}
		Deck->bIsUISound = true; // keeps playing under pause menus
		Deck->FadeIn(Id == Track ? F : 0.f, Id == Track ? 1.f : SilentLevel, 0.f, EAudioFaderCurve::Sin);
		Decks.Add(Id, Deck);
		Started.Add(Id);
	}
	// Running decks: the target rises, layered siblings fall silent but keep time, the rest fade out and stop.
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& D : Decks)
	{
		if (!D.Value || !D.Value->IsPlaying() || D.Value->GetPlayState() == EAudioComponentPlayState::FadingOut || Started.Contains(D.Key))
		{
			continue;
		}
		if (D.Key == Track || Keep.Contains(D.Key))
		{
			D.Value->AdjustVolume(F, D.Key == Track ? 1.f : SilentLevel, EAudioFaderCurve::Sin);
			continue;
		}
		D.Value->FadeOut(F, 0.f, EAudioFaderCurve::Sin);
	}
}

float UDCMusicSubsystem::TrackLevel(FName Track) const
{
	const FDCMusicTrack* Row = UDCTuning::Get().FindMusicTrack(Track);
	const UGameInstance* GI = GetGameInstance();
	return (Row ? Row->Volume : 1.f) * UDCTuning::Get().Music.Volume * Duck * DCAudio::Master(GI ? GI->GetWorld() : nullptr);
}

float UDCMusicSubsystem::TrackSeconds(FName Track) const
{
	const USoundWave* Wave = Cast<USoundWave>(Sounds.FindRef(Track));
	return Wave ? Wave->Duration : 0.f; // GetDuration() reports "forever" for a looping wave
}

void UDCMusicSubsystem::ApplyLevels()
{
	const UGameInstance* GI = GetGameInstance();
	AppliedMaster = DCAudio::Master(GI ? GI->GetWorld() : nullptr);
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& D : Decks)
	{
		if (D.Value)
		{
			D.Value->SetVolumeMultiplier(TrackLevel(D.Key));
		}
	}
}
