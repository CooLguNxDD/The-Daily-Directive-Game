#pragma once

#include "CoreMinimal.h"
#include "Rules/DCPhotoRules.h"
#include "DCTypes.generated.h"

UENUM(BlueprintType)
enum class EDCSlot : uint8
{
	Rifle,
	Stratagem,
	Camera,
};

UENUM(BlueprintType)
enum class EDCRunPhase : uint8
{
	Lobby,
	Sector,
	Draft,
	Reel,
	FinalCut,
	Vote,
	FrontPage,
};

/** A detonation the host recorded. Photo scoring reads these, never debris. */
USTRUCT(BlueprintType)
struct FDCBlastRecord
{
	GENERATED_BODY()
	UPROPERTY() int32 Id = 0;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() float Radius = 0.f;
	UPROPERTY() FName StratagemId;
};

/** A graded photo. The image itself stays local to the photographer's machine. */
USTRUCT(BlueprintType)
struct FDCPhotoRecord
{
	GENERATED_BODY()
	UPROPERTY() int32 PhotoId = 0;
	UPROPERTY() int32 OwnerPlayerId = 0;
	UPROPERTY() int32 Sector = 0;
	UPROPERTY() float ShotTime = 0.f;
	UPROPERTY() FVector_NetQuantize10 CameraLocation;
	UPROPERTY() FRotator CameraRotation = FRotator::ZeroRotator;
	UPROPERTY() float FOV = 60.f;
	UPROPERTY() FDCPhotoGrade Grade;
	/** Blast the photo framed (0 = none), for the reel's re-staging. */
	UPROPERTY() int32 BlastId = 0;
};

/** A building the host collapsed: the reel rebuilds the sector from its seed and replays these. */
USTRUCT(BlueprintType)
struct FDCCollapseRecord
{
	GENERATED_BODY()
	UPROPERTY() int32 Sector = 0;
	UPROPERTY() int32 LotIndex = 0;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() FVector_NetQuantize Epicenter;
};

/** One sample of a player's pose, recorded by the host for the reel's re-staging. */
USTRUCT(BlueprintType)
struct FDCPoseSample
{
	GENERATED_BODY()
	UPROPERTY() float Time = 0.f;
	UPROPERTY() FVector_NetQuantize10 Location;
	UPROPERTY() float Yaw = 0.f;
	UPROPERTY() float Pitch = 0.f;
	UPROPERTY() float Speed = 0.f;
	UPROPERTY() bool bCamera = false;
};

/** A re-staged highlight: the shot, its 2-second pose buffer and the blast it framed. */
USTRUCT(BlueprintType)
struct FDCReelClip
{
	GENERATED_BODY()
	UPROPERTY() FDCPhotoRecord Photo;
	UPROPERTY() TArray<FDCPoseSample> Poses;
	UPROPERTY() FString PlayerName;
};

/** The live HQ Scoop (R8). Index into UDCTuning::Scoops; -1 = none. */
USTRUCT(BlueprintType)
struct FDCScoopState
{
	GENERATED_BODY()
	UPROPERTY() int32 Index = -1;
	UPROPERTY() float EndTime = 0.f;
	UPROPERTY() bool bDone = false;
	UPROPERTY() FString CompletedBy;
};

/** A front-page pick as every machine sees it. */
USTRUCT(BlueprintType)
struct FDCPagePick
{
	GENERATED_BODY()
	UPROPERTY() FDCPhotoRecord Photo;
	UPROPERTY() FString PlayerName;
	UPROPERTY() int32 Votes = 0;
	UPROPERTY() int32 PageScore = 0;
};
