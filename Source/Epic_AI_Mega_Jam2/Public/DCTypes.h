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
