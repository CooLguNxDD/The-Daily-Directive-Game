// R13 remix data: every +CueMix row names a real cue once, its trim is sane, and a remixed recording loads as a
// one-shot sound wave (never looping, so a cue cannot hang on).
#include "DCAudio.h"
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCCueMix.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCCueMixDataTest, "Epic_AI_Mega_Jam2.Audio.CueMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCCueMixDataTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	const TArray<FName> Cues = DCAudio::CueNames();
	TSet<FName> Seen;
	for (const FDCCueMix& Row : T.CueMix)
	{
		const FString Id = Row.Cue.ToString();
		TestTrue(*FString::Printf(TEXT("+CueMix %s is a cue in the bank"), *Id), Cues.Contains(Row.Cue));
		TestFalse(*FString::Printf(TEXT("+CueMix %s is listed once"), *Id), Seen.Contains(Row.Cue));
		Seen.Add(Row.Cue);
		TestTrue(*FString::Printf(TEXT("+CueMix %s trim is in 0.1..4"), *Id), Row.Volume >= 0.1f && Row.Volume <= 4.f);
		if (Row.Sound.IsNull())
		{
			continue;
		}
		const USoundWave* Wave = Cast<USoundWave>(Row.Sound.TryLoad());
		TestNotNull(*FString::Printf(TEXT("+CueMix %s remix loads"), *Id), Wave);
		TestTrue(*FString::Printf(TEXT("+CueMix %s remix is a one-shot"), *Id), Wave && !Wave->bLooping);
	}
	TestEqual(TEXT("a cue without a row is untrimmed"), DCCueMix::Trim(T.CueMix, TEXT("NoSuchCue")), 1.f);
	TArray<FDCCueMix> Wild;
	Wild.AddDefaulted();
	Wild[0].Cue = TEXT("X");
	Wild[0].Volume = 50.f;
	TestEqual(TEXT("an out-of-range trim is clamped"), DCCueMix::Trim(Wild, TEXT("X")), 4.f);
	return true;
}

#endif
