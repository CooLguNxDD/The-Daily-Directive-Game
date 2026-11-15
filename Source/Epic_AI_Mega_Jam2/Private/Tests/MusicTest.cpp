// R14 music: the pure intensity and layer rules, and that the shipped music data resolves (every phase and layer names
// a +MusicTracks row, the layers climb, the menu has a set, every track is an imported looping sound wave).
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCMusicRules.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCMusicRulesTest, "Epic_AI_Mega_Jam2.Music.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCMusicRulesTest::RunTest(const FString&)
{
	FDCMusicTuning M;
	M.HostilesForFull = 4.f;
	M.HostileWeight = 0.6f;
	M.BlastWeight = 0.5f;
	M.BlastRadiusCm = 10000.f;
	M.BlastHoldSeconds = 10.f;
	M.TelegraphShare = 0.4f;
	TestEqual(TEXT("nothing near is calm"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>()), 0.f);
	TestEqual(TEXT("half the hostiles, half the share"), DCMusicRules::Intensity(M, 2, TArray<FDCMusicBlast>()), 0.3f, 1e-4f);
	TestEqual(TEXT("the hostile share caps"), DCMusicRules::Intensity(M, 40, TArray<FDCMusicBlast>()), 0.6f, 1e-4f);
	const FDCMusicBlast Fresh{0.f, 2000.f, 3.f};
	TestEqual(TEXT("a fresh blast near adds its weight"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{Fresh}), 0.5f, 1e-4f);
	TestEqual(TEXT("a blast fades over the hold"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{FDCMusicBlast{5.f, 2000.f, 8.f}}), 0.25f, 1e-4f);
	TestEqual(TEXT("an old blast is gone"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{FDCMusicBlast{11.f, 2000.f, 14.f}}), 0.f);
	TestEqual(TEXT("a far blast does not count"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{FDCMusicBlast{0.f, 20000.f, 3.f}}), 0.f);
	TestEqual(TEXT("a telegraphing strike counts its share"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{FDCMusicBlast{-2.f, 2000.f, 1.f}}), 0.2f, 1e-4f);
	TestEqual(TEXT("a strike not yet telegraphed does not count"), DCMusicRules::Intensity(M, 0, TArray<FDCMusicBlast>{FDCMusicBlast{-4.f, 2000.f, -1.f}}), 0.f);
	TestEqual(TEXT("intensity is clamped to 1"), DCMusicRules::Intensity(M, 40, TArray<FDCMusicBlast>{Fresh, Fresh}), 1.f);

	M.RiseSeconds = 0.5f;
	M.FallSeconds = 5.f;
	const float Up = DCMusicRules::Smooth(M, 0.f, 1.f, 0.25f);
	const float Down = 1.f - DCMusicRules::Smooth(M, 1.f, 0.f, 0.25f);
	TestTrue(TEXT("intensity rises faster than it falls"), Up > Down * 3.f);
	TestTrue(TEXT("smoothing moves toward the target"), Up > 0.f && Up < 1.f);

	TArray<FDCMusicLayer> L;
	L.AddDefaulted(2);
	L[0].EnterAt = 0.f;
	L[1].EnterAt = 0.4f;
	L[1].ExitAt = 0.2f;
	L[1].MinHoldSeconds = 8.f;
	TestEqual(TEXT("fresh pick at calm"), DCMusicRules::PickLayer(L, INDEX_NONE, 0.1f, 0.f), 0);
	TestEqual(TEXT("fresh pick in a fight"), DCMusicRules::PickLayer(L, INDEX_NONE, 0.6f, 0.f), 1);
	TestEqual(TEXT("escalates at once"), DCMusicRules::PickLayer(L, 0, 0.45f, 0.f), 1);
	TestEqual(TEXT("stays between exit and enter (hysteresis)"), DCMusicRules::PickLayer(L, 1, 0.3f, 60.f), 1);
	TestEqual(TEXT("holds its minimum time"), DCMusicRules::PickLayer(L, 1, 0.05f, 3.f), 1);
	TestEqual(TEXT("steps down after the hold"), DCMusicRules::PickLayer(L, 1, 0.05f, 9.f), 0);
	TestEqual(TEXT("no layers, nothing"), DCMusicRules::PickLayer(TArray<FDCMusicLayer>(), INDEX_NONE, 0.5f, 0.f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("playlist wraps"), DCMusicRules::NextSlot(1, 2), 0);
	TestEqual(TEXT("empty playlist stays at 0"), DCMusicRules::NextSlot(3, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCMusicDataTest, "Epic_AI_Mega_Jam2.Music.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCMusicDataTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	TestTrue(TEXT("music tracks are shipped"), T.MusicTracks.Num() >= 3);
	for (const FDCMusicTrack& Track : T.MusicTracks)
	{
		const USoundWave* Wave = Cast<USoundWave>(Track.Sound.TryLoad());
		TestNotNull(*FString::Printf(TEXT("track %s is an imported sound wave"), *Track.Id.ToString()), Wave);
		if (Wave)
		{
			TestTrue(*FString::Printf(TEXT("track %s loops"), *Track.Id.ToString()), Wave->bLooping);
			TestTrue(*FString::Printf(TEXT("track %s is a real loop (>20 s)"), *Track.Id.ToString()), Wave->Duration > 20.f);
		}
	}
	TestTrue(TEXT("two combat layers at least"), T.MusicLayers.Num() >= 2);
	for (int32 i = 0; i < T.MusicLayers.Num(); ++i)
	{
		const FDCMusicLayer& L = T.MusicLayers[i];
		TestNotNull(*FString::Printf(TEXT("layer %s names a track"), *L.Id.ToString()), T.FindMusicTrack(L.Track));
		TestTrue(TEXT("layers climb"), i == 0 ? L.EnterAt == 0.f : L.EnterAt > T.MusicLayers[i - 1].EnterAt);
		TestTrue(TEXT("a layer leaves below where it enters"), i == 0 || L.ExitAt < L.EnterAt);
		TestTrue(TEXT("fades are positive"), L.FadeInSeconds > 0.f);
	}
	const FDCMusicPhase* Lobby = T.FindMusicPhase(EDCRunPhase::Lobby);
	TestTrue(TEXT("the menu plays a set of tracks"), Lobby && !Lobby->bByIntensity && Lobby->Playlist.Num() >= 2);
	const FDCMusicPhase* Sector = T.FindMusicPhase(EDCRunPhase::Sector);
	TestTrue(TEXT("a sector plays by combat intensity"), Sector && Sector->bByIntensity);
	for (const FDCMusicPhase& P : T.MusicPhases)
	{
		for (const FName& Id : P.Playlist)
		{
			TestNotNull(*FString::Printf(TEXT("playlist track %s is a +MusicTracks row"), *Id.ToString()), T.FindMusicTrack(Id));
		}
	}
	return true;
}

#endif
