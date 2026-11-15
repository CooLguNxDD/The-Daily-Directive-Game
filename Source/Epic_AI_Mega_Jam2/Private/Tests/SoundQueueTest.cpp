// R13 sound queue (pure rule, Rules/DCSoundQueue) and the shipped sound classes (Config/DefaultGame.ini): merging,
// voice and start budgets, protected classes, pitch clamp, and that every class cue is a real cue. Runs under -nullrhi.
#include "DCAudio.h"
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCSoundQueue.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FDCSoundClassDef Class(const TCHAR* Id, int32 Priority, int32 MaxVoices, int32 Starts, TArray<FName> Cues)
	{
		FDCSoundClassDef C;
		C.Id = Id;
		C.Priority = Priority;
		C.MaxVoices = MaxVoices;
		C.StartsPerFrame = Starts;
		C.MaxWaitFrames = 2;
		C.PitchMin = 0.9f;
		C.PitchMax = 1.1f;
		C.Cues = MoveTemp(Cues);
		return C;
	}

	FDCSoundRequest At(const TCHAR* Cue, const FVector& Where, float Volume = 1.f, float Pitch = 1.f)
	{
		FDCSoundRequest R;
		R.Cue = Cue;
		R.Location = Where;
		R.Volume = Volume;
		R.Pitch = Pitch;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCSoundQueueTest, "Epic_AI_Mega_Jam2.Audio.SoundQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCSoundQueueTest::RunTest(const FString&)
{
	const TArray<FDCSoundClassDef> Classes = {
		Class(TEXT("Strike"), 100, 4, 4, {TEXT("Boom")}),
		Class(TEXT("Crumbs"), 10, 3, 2, {TEXT("Tick")}),
		Class(TEXT("Default"), 50, 8, 8, {}),
	};
	FDCSoundMixTuning Mix;
	Mix.TotalVoices = 3;
	Mix.ProtectedPriority = 80;
	auto Seconds = [](FName) { return 1.f; };

	// Merge: five crumb ticks on one spot become one louder play; a far one stays separate.
	TArray<FDCSoundRequest> Pending = {At(TEXT("Tick"), FVector::ZeroVector, 0.5f), At(TEXT("Tick"), FVector(10, 0, 0), 0.5f), At(TEXT("Tick"), FVector(20, 0, 0), 0.5f),
		At(TEXT("Tick"), FVector(30, 0, 0), 0.5f), At(TEXT("Tick"), FVector(40, 0, 0), 0.5f), At(TEXT("Tick"), FVector(50000, 0, 0), 0.5f)};
	TArray<FDCSoundVoice> Voices;
	TArray<FDCSoundRequest> Plays;
	TArray<FDCSoundDropped> Drops;
	DCSoundQueue::Flush(Classes, Mix, 0.0, Seconds, Pending, Voices, Plays, Drops);
	TestEqual(TEXT("near ticks merge into one play, the far one plays too"), Plays.Num(), 2);
	TestEqual(TEXT("the merged play counts four merges"), Plays.Num() > 0 ? Plays[0].Merged : -1, 4);
	TestTrue(TEXT("a merged play is louder than one request"), Plays.Num() > 0 && Plays[0].Volume > 0.5f);

	// The same cue right after it started, at the same spot: merged away, not stacked.
	Pending = {At(TEXT("Tick"), FVector::ZeroVector)};
	Plays.Reset();
	Drops.Reset();
	DCSoundQueue::Flush(Classes, Mix, 0.01, Seconds, Pending, Voices, Plays, Drops);
	TestEqual(TEXT("a repeat inside the merge window does not stack"), Plays.Num(), 0);
	TestTrue(TEXT("logged as merged"), Drops.Num() == 1 && Drops[0].Why == EDCSoundDrop::Merged);

	// Budgets: the crumb class holds 3 voices, starts 2 per frame; the shared cap is 3 for unprotected classes.
	Voices.Reset();
	Pending.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		Pending.Add(At(TEXT("Tick"), FVector(i * 10000.f, 0, 0)));
	}
	Plays.Reset();
	Drops.Reset();
	DCSoundQueue::Flush(Classes, Mix, 1.0, Seconds, Pending, Voices, Plays, Drops);
	TestEqual(TEXT("two crumb starts per frame"), Plays.Num(), 2);
	TestEqual(TEXT("the others wait"), Pending.Num(), 2);
	// Strikes are protected: the shared cap is full of crumbs, a strike still starts first.
	Pending.Add(At(TEXT("Boom"), FVector::ZeroVector, 1.f, 1.6f));
	Pending.Add(At(TEXT("Other"), FVector(0, 9000, 0)));
	Plays.Reset();
	Drops.Reset();
	DCSoundQueue::Flush(Classes, Mix, 1.02, Seconds, Pending, Voices, Plays, Drops);
	TestTrue(TEXT("the strike starts first"), Plays.Num() > 0 && Plays[0].Cue == TEXT("Boom"));
	TestEqual(TEXT("strike pitch is clamped to its class range"), Plays.Num() > 0 ? Plays[0].Pitch : 0.f, 1.1f);
	// Waiting requests drop after MaxWaitFrames when no voice frees.
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		Pending.Add(At(TEXT("Tick"), FVector(0, Frame * 10000.f, 0)));
		DCSoundQueue::Flush(Classes, Mix, 1.03 + Frame * 0.016, Seconds, Pending, Voices, Plays, Drops);
	}
	TestTrue(TEXT("over budget past the wait: dropped"), Drops.ContainsByPredicate([](const FDCSoundDropped& D) { return D.Why == EDCSoundDrop::Budget; }));
	TestTrue(TEXT("never more crumb voices than the budget"), Voices.FilterByPredicate([](const FDCSoundVoice& V) { return V.Class == 1; }).Num() <= 3);

	// Shipped data: every class cue is a cue the bank knows, and the classes the human named exist and are protected.
	const UDCTuning& T = UDCTuning::Get();
	const TArray<FName> Known = DCAudio::CueNames();
	TArray<FName> Data;
	for (const FDCSoundRecipe& R : T.SoundRecipes)
	{
		Data.Add(R.Id);
	}
	for (const FDCSoundClassDef& C : T.SoundClasses)
	{
		for (const FName Cue : C.Cues)
		{
			TestTrue(*FString::Printf(TEXT("class %s cue %s is a cue"), *C.Id.ToString(), *Cue.ToString()), Known.Contains(Cue) || Data.Contains(Cue));
		}
		TestTrue(*FString::Printf(TEXT("class %s pitch range is narrow"), *C.Id.ToString()), C.PitchMin >= 0.85f && C.PitchMax <= 1.15f && C.PitchMin <= C.PitchMax);
	}
	for (const TCHAR* Id : {TEXT("Strike"), TEXT("UI"), TEXT("PlayerGun")})
	{
		const FDCSoundClassDef* C = T.SoundClasses.FindByPredicate([Id](const FDCSoundClassDef& R) { return R.Id == Id; });
		TestTrue(*FString::Printf(TEXT("%s is protected from the shared cap"), Id), C && C->Priority >= T.SoundMix.ProtectedPriority);
	}
	TestTrue(TEXT("a Default class exists"), DCSoundQueue::ClassOf(T.SoundClasses, TEXT("NoSuchCue")) != INDEX_NONE);
	return true;
}

#endif
