// Save manager (R24): versioned local saves under Saved/DCSave for characters, settings and the photo album.
// One file per kind, JSON with a schema "Version"; older files migrate step by step through a registry (one function
// per version step, never a ladder); writes are atomic (temp file, then replace, the old file kept as .prev); a
// corrupt, newer or missing file falls back to defaults without a crash (the corrupt one is kept as .bad).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DCSave.generated.h"

class FJsonObject;

USTRUCT()
struct FDCSettingsSave
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 1;
	UPROPERTY() float MasterVolume = 1.f;
	UPROPERTY() float MouseSensitivity = 1.f;
	/** Post scalability preset index (0 low .. 2 high) and window mode. */
	UPROPERTY() int32 Quality = 2;
	UPROPERTY() bool bFullscreen = false;
	/** Rebound actions: action name -> key name (empty = the defaults). */
	UPROPERTY() TMap<FString, FString> KeyBindings;
};

USTRUCT()
struct FDCCharacterSave
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 1;
	UPROPERTY() int32 ActiveSlot = 0;
	/** Character recipes as share codes (R23), one per slot. */
	UPROPERTY() TArray<FString> Slots;
};

USTRUCT()
struct FDCAlbumEntry
{
	GENERATED_BODY()
	/** "photo" or "frontpage". */
	UPROPERTY() FString Kind;
	/** PNG beside the album file (relative name). */
	UPROPERTY() FString Image;
	UPROPERTY() int32 Score = 0;
	UPROPERTY() FString Headline;
	UPROPERTY() FString CallSign;
	UPROPERTY() FString Date;
};

USTRUCT()
struct FDCAlbumSave
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FDCAlbumEntry> Entries;
};

namespace DCSave
{
	/** One migration step: edits the raw JSON of version N into version N + 1. */
	using FMigration = TFunction<void(FJsonObject&)>;

	/** What a load did (for logs and tests). */
	enum class ELoadResult : uint8 { Loaded, Migrated, Missing, Corrupt, TooNew, MigrationGap };

	/** Pure: parse Text into Out (a USTRUCT with an int32 Version). Steps[N] migrates N -> N+1 up to CurrentVersion.
	 *  Anything but Loaded/Migrated leaves Out at its defaults. */
	EPIC_AI_MEGA_JAM2_API ELoadResult FromJson(const FString& Text, const UScriptStruct* Type, void* Out, int32 CurrentVersion, const TMap<int32, FMigration>& Steps);
	EPIC_AI_MEGA_JAM2_API FString ToJson(const UScriptStruct* Type, const void* In);

	/** Files: read with fallback (a corrupt file is moved aside to <file>.bad), atomic write (keeps <file>.prev). */
	EPIC_AI_MEGA_JAM2_API ELoadResult LoadFile(const FString& Path, const UScriptStruct* Type, void* Out, int32 CurrentVersion, const TMap<int32, FMigration>& Steps);
	EPIC_AI_MEGA_JAM2_API bool SaveFile(const FString& Path, const UScriptStruct* Type, const void* In);

	EPIC_AI_MEGA_JAM2_API const TCHAR* ResultName(ELoadResult Result);
}

/** Owns the three save files for the session: loaded at start, saved on change. */
UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UDCSaveSubsystem* Get(const UObject* WorldContext);
	static FString Folder();

	FDCSettingsSave Settings;
	FDCCharacterSave Characters;
	FDCAlbumSave Album;

	void SaveSettings();
	void SaveCharacters();
	/** Adds a printed photo (BGRA pixels) to the album as a PNG and an entry; the oldest drop past the album limit. */
	void AddToAlbum(const FString& Kind, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 Score, const FString& Headline, const FString& CallSign);

private:
	void SaveAlbum();
};
