#include "DCSave.h"

#include "DCTuning.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogDCSave, Log, All);

namespace DCSave
{
	const TCHAR* ResultName(ELoadResult Result)
	{
		static const TMap<ELoadResult, const TCHAR*> Names = {
			{ELoadResult::Loaded, TEXT("loaded")}, {ELoadResult::Migrated, TEXT("migrated")}, {ELoadResult::Missing, TEXT("missing")},
			{ELoadResult::Corrupt, TEXT("corrupt")}, {ELoadResult::TooNew, TEXT("too-new")}, {ELoadResult::MigrationGap, TEXT("migration-gap")}};
		const TCHAR* const* Name = Names.Find(Result);
		return Name ? *Name : TEXT("?");
	}

	ELoadResult FromJson(const FString& Text, const UScriptStruct* Type, void* Out, int32 CurrentVersion, const TMap<int32, FMigration>& Steps)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return ELoadResult::Corrupt;
		}
		int32 Version = 1;
		Root->TryGetNumberField(TEXT("Version"), Version);
		if (Version > CurrentVersion)
		{
			return ELoadResult::TooNew; // written by a newer build: never guessed at
		}
		const bool bMigrated = Version < CurrentVersion;
		for (; Version < CurrentVersion; ++Version)
		{
			const FMigration* Step = Steps.Find(Version);
			if (!Step)
			{
				return ELoadResult::MigrationGap;
			}
			(*Step)(*Root);
		}
		Root->SetNumberField(TEXT("Version"), CurrentVersion);
		// Parse into a scratch copy so a half-read struct never replaces the defaults.
		TArray<uint8> Scratch;
		Scratch.SetNumZeroed(Type->GetStructureSize());
		Type->InitializeStruct(Scratch.GetData());
		Type->CopyScriptStruct(Scratch.GetData(), Out);
		const bool bOk = FJsonObjectConverter::JsonObjectToUStruct(Root.ToSharedRef(), Type, Scratch.GetData());
		if (bOk)
		{
			Type->CopyScriptStruct(Out, Scratch.GetData());
		}
		Type->DestroyStruct(Scratch.GetData());
		return bOk ? (bMigrated ? ELoadResult::Migrated : ELoadResult::Loaded) : ELoadResult::Corrupt;
	}

	FString ToJson(const UScriptStruct* Type, const void* In)
	{
		FString Text;
		FJsonObjectConverter::UStructToJsonObjectString(Type, In, Text);
		return Text;
	}

	ELoadResult LoadFile(const FString& Path, const UScriptStruct* Type, void* Out, int32 CurrentVersion, const TMap<int32, FMigration>& Steps)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return ELoadResult::Missing;
		}
		const ELoadResult Result = FromJson(Text, Type, Out, CurrentVersion, Steps);
		if (Result == ELoadResult::Corrupt || Result == ELoadResult::MigrationGap)
		{
			// Keep the bad file for a human to look at; the next save writes a clean one.
			IFileManager::Get().Move(*(Path + TEXT(".bad")), *Path, true);
		}
		UE_LOG(LogDCSave, Display, TEXT("DCSAVE load %s: %s"), *FPaths::GetCleanFilename(Path), ResultName(Result));
		return Result;
	}

	bool SaveFile(const FString& Path, const UScriptStruct* Type, const void* In)
	{
		const FString Tmp = Path + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(ToJson(Type, In), *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogDCSave, Warning, TEXT("DCSAVE write failed: %s"), *Tmp);
			return false;
		}
		// Atomic swap: the last good file becomes .prev (history), the temp file takes its place.
		if (IFileManager::Get().FileExists(*Path))
		{
			IFileManager::Get().Copy(*(Path + TEXT(".prev")), *Path);
		}
		const bool bMoved = IFileManager::Get().Move(*Path, *Tmp, true);
		UE_LOG(LogDCSave, Display, TEXT("DCSAVE save %s: %s"), *FPaths::GetCleanFilename(Path), bMoved ? TEXT("ok") : TEXT("FAILED"));
		return bMoved;
	}
}

namespace
{
	// Schema registry: the current version of each file and its migration steps (a new version adds one step).
	struct FSaveKind
	{
		const TCHAR* File;
		int32 Version;
		TMap<int32, DCSave::FMigration> Steps;
	};
	const FSaveKind& SettingsKind() { static const FSaveKind K{TEXT("settings.json"), 1, {}}; return K; }
	const FSaveKind& CharactersKind() { static const FSaveKind K{TEXT("characters.json"), 1, {}}; return K; }
	const FSaveKind& AlbumKind() { static const FSaveKind K{TEXT("album.json"), 1, {}}; return K; }
}

FString UDCSaveSubsystem::Folder()
{
	return FPaths::ProjectSavedDir() / TEXT("DCSave");
}

UDCSaveSubsystem* UDCSaveSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDCSaveSubsystem>() : nullptr;
}

void UDCSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IFileManager::Get().MakeDirectory(*Folder(), true);
	DCSave::LoadFile(Folder() / SettingsKind().File, FDCSettingsSave::StaticStruct(), &Settings, SettingsKind().Version, SettingsKind().Steps);
	DCSave::LoadFile(Folder() / CharactersKind().File, FDCCharacterSave::StaticStruct(), &Characters, CharactersKind().Version, CharactersKind().Steps);
	DCSave::LoadFile(Folder() / AlbumKind().File, FDCAlbumSave::StaticStruct(), &Album, AlbumKind().Version, AlbumKind().Steps);
}

void UDCSaveSubsystem::SaveSettings()
{
	Settings.Version = SettingsKind().Version;
	DCSave::SaveFile(Folder() / SettingsKind().File, FDCSettingsSave::StaticStruct(), &Settings);
}

void UDCSaveSubsystem::SaveCharacters()
{
	Characters.Version = CharactersKind().Version;
	DCSave::SaveFile(Folder() / CharactersKind().File, FDCCharacterSave::StaticStruct(), &Characters);
}

void UDCSaveSubsystem::SaveAlbum()
{
	Album.Version = AlbumKind().Version;
	DCSave::SaveFile(Folder() / AlbumKind().File, FDCAlbumSave::StaticStruct(), &Album);
}

void UDCSaveSubsystem::AddToAlbum(const FString& Kind, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 Score, const FString& Headline, const FString& CallSign)
{
	if (Pixels.Num() != Width * Height || Width <= 0)
	{
		return;
	}
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		return;
	}
	const FDateTime Now = FDateTime::Now();
	FDCAlbumEntry Entry;
	Entry.Kind = Kind;
	Entry.Image = FString::Printf(TEXT("album/%s_%s_%d.png"), *Now.ToString(TEXT("%Y%m%d-%H%M%S")), *Kind, Score);
	Entry.Score = Score;
	Entry.Headline = Headline;
	Entry.CallSign = CallSign;
	Entry.Date = Now.ToIso8601();
	const FString File = Folder() / Entry.Image;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	if (!FFileHelper::SaveArrayToFile(Png->GetCompressed(), *File))
	{
		return;
	}
	Album.Entries.Add(MoveTemp(Entry));
	const int32 Max = FMath::Max(1, UDCTuning::Get().Run.AlbumMaxEntries);
	while (Album.Entries.Num() > Max)
	{
		IFileManager::Get().Delete(*(Folder() / Album.Entries[0].Image));
		Album.Entries.RemoveAt(0);
	}
	SaveAlbum();
}
