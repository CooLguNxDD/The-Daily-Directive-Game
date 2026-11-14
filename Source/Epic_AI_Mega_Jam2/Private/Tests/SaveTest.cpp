// Save manager (R24): versioned JSON, migration steps, corrupt / newer / gapped files fall back to defaults, and a
// save round-trips. Pure parts plus a temp-folder file round trip; runs under -nullrhi.
#include "DCSave.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SaveFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCSaveSchemaTest, "Epic_AI_Mega_Jam2.Save.Schema", SaveFlags)
bool FDCSaveSchemaTest::RunTest(const FString&)
{
	using namespace DCSave;
	const TMap<int32, FMigration> None;

	// Round trip.
	FDCSettingsSave In;
	In.MasterVolume = 0.4f;
	In.MouseSensitivity = 1.7f;
	In.KeyBindings.Add(TEXT("Fire"), TEXT("RightMouseButton"));
	FDCSettingsSave Out;
	TestEqual(TEXT("round trip loads"), static_cast<int32>(FromJson(ToJson(FDCSettingsSave::StaticStruct(), &In), FDCSettingsSave::StaticStruct(), &Out, 1, None)), static_cast<int32>(ELoadResult::Loaded));
	TestEqual(TEXT("volume kept"), Out.MasterVolume, 0.4f);
	TestEqual(TEXT("binding kept"), Out.KeyBindings.FindRef(TEXT("Fire")), FString(TEXT("RightMouseButton")));

	// Corrupt text: defaults, no crash.
	FDCSettingsSave Bad;
	Bad.MasterVolume = 0.9f;
	TestEqual(TEXT("corrupt is reported"), static_cast<int32>(FromJson(TEXT("{ not json"), FDCSettingsSave::StaticStruct(), &Bad, 1, None)), static_cast<int32>(ELoadResult::Corrupt));
	TestEqual(TEXT("corrupt leaves the defaults"), Bad.MasterVolume, 0.9f);

	// A newer file is never guessed at.
	FDCSettingsSave Newer;
	TestEqual(TEXT("too new"), static_cast<int32>(FromJson(TEXT("{\"Version\": 9, \"MasterVolume\": 0.1}"), FDCSettingsSave::StaticStruct(), &Newer, 1, None)), static_cast<int32>(ELoadResult::TooNew));
	TestEqual(TEXT("too new keeps defaults"), Newer.MasterVolume, 1.f);

	// Migration: version 1 stored "Volume"; version 2 renamed it MasterVolume (a step, not a branch).
	TMap<int32, FMigration> Steps;
	Steps.Add(1, [](FJsonObject& J)
	{
		double V = 1.0;
		if (J.TryGetNumberField(TEXT("Volume"), V))
		{
			J.SetNumberField(TEXT("MasterVolume"), V);
		}
	});
	FDCSettingsSave Migrated;
	TestEqual(TEXT("migrated"), static_cast<int32>(FromJson(TEXT("{\"Version\": 1, \"Volume\": 0.25}"), FDCSettingsSave::StaticStruct(), &Migrated, 2, Steps)), static_cast<int32>(ELoadResult::Migrated));
	TestEqual(TEXT("migrated value"), Migrated.MasterVolume, 0.25f);
	FDCSettingsSave Gap;
	TestEqual(TEXT("missing step"), static_cast<int32>(FromJson(TEXT("{\"Version\": 1}"), FDCSettingsSave::StaticStruct(), &Gap, 3, Steps)), static_cast<int32>(ELoadResult::MigrationGap));

	// Files: atomic write with history, a corrupt file moved aside, a missing file reported.
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("DCSaveTest");
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / TEXT("settings.json");
	FDCSettingsSave Missing;
	TestEqual(TEXT("missing file"), static_cast<int32>(LoadFile(File, FDCSettingsSave::StaticStruct(), &Missing, 1, None)), static_cast<int32>(ELoadResult::Missing));
	TestTrue(TEXT("first save"), SaveFile(File, FDCSettingsSave::StaticStruct(), &In));
	In.MasterVolume = 0.6f;
	TestTrue(TEXT("second save"), SaveFile(File, FDCSettingsSave::StaticStruct(), &In));
	TestTrue(TEXT("history kept"), IFileManager::Get().FileExists(*(File + TEXT(".prev"))));
	TestFalse(TEXT("no temp left"), IFileManager::Get().FileExists(*(File + TEXT(".tmp"))));
	FDCSettingsSave Reloaded;
	LoadFile(File, FDCSettingsSave::StaticStruct(), &Reloaded, 1, None);
	TestEqual(TEXT("file round trip"), Reloaded.MasterVolume, 0.6f);
	FFileHelper::SaveStringToFile(TEXT("garbage{{"), *File);
	FDCSettingsSave AfterCorrupt;
	TestEqual(TEXT("corrupt file"), static_cast<int32>(LoadFile(File, FDCSettingsSave::StaticStruct(), &AfterCorrupt, 1, None)), static_cast<int32>(ELoadResult::Corrupt));
	TestTrue(TEXT("corrupt file kept aside"), IFileManager::Get().FileExists(*(File + TEXT(".bad"))));
	TestEqual(TEXT("corrupt file gives defaults"), AfterCorrupt.MasterVolume, 1.f);
	// A crash inside the swap leaves only .prev (the main file deleted before the rename): the last good save comes back.
	IFileManager::Get().Delete(*File);
	FDCSettingsSave Recovered;
	const ELoadResult R = LoadWithHistory(File, FDCSettingsSave::StaticStruct(), &Recovered, 1, None);
	TestTrue(TEXT("missing main file recovers from .prev"), R == ELoadResult::Loaded || R == ELoadResult::Migrated);
	TestTrue(TEXT("recovered values are the last good save"), Recovered.MasterVolume < 0.99f);
	TestTrue(TEXT("main file written back"), IFileManager::Get().FileExists(*File));
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCAlbumNamesTest, "Epic_AI_Mega_Jam2.Save.AlbumNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCAlbumNamesTest::RunTest(const FString&)
{
	// Two pictures of the same kind and score in the same second never share a file (CP5 review: they overwrote).
	UDCSaveSubsystem* Save = NewObject<UDCSaveSubsystem>(NewObject<UGameInstance>()); // a subsystem lives in a game instance
	TSet<FString> Seen;
	for (int32 i = 0; i < 5; ++i)
	{
		FDCAlbumEntry E;
		E.Kind = TEXT("photo");
		E.Score = 600;
		E.Image = Save->NewAlbumImage(E.Kind);
		TestFalse(FString::Printf(TEXT("%s is new"), *E.Image), Seen.Contains(E.Image));
		Seen.Add(E.Image);
		Save->Album.Entries.Add(E);
	}
	return true;
}

#endif
