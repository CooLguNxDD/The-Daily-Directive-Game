// Gameplay-layer measurement of the 3D scene at the shutter moment. Reads host facts only (blast records,
// building collapse times, enemy positions), never debris pieces or pixels, and fills FDCPhotoInputs for the pure
// grading rules. The host runs it for the real score; the photographer's client runs it for the instant preview.
#pragma once

#include "CoreMinimal.h"
#include "Rules/DCPhotoRules.h"

class UWorld;
class AActor;

struct FDCShotView
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float FOV = 60.f;
	float Aspect = 16.f / 9.f;
};

struct FDCLiveTag
{
	FVector World = FVector::ZeroVector;
	FName Tag;
	/** 0..1 how strongly it pulses (fresh blast = 1). */
	float Heat = 0.f;
};

namespace DCPhotoProbe
{
	/** Normalised frame coords in [-1, 1] when P is in front of the camera; false when behind. */
	EPIC_AI_MEGA_JAM2_API bool ProjectToFrame(const FDCShotView& View, const FVector& P, FVector2D& OutNdc);
	EPIC_AI_MEGA_JAM2_API bool InFrame(const FDCShotView& View, const FVector& P, float Margin = 1.f);

	EPIC_AI_MEGA_JAM2_API FDCPhotoInputs Measure(UWorld* World, const FDCShotView& View, float ShotTime, const AActor* Photographer, bool bSpotlit, int32& OutBlastId);

	/** Things worth shooting in the viewfinder right now, for the floating tags (R2). */
	EPIC_AI_MEGA_JAM2_API void CollectTags(UWorld* World, const FDCShotView& View, float Now, const AActor* Photographer, TArray<FDCLiveTag>& Out);
}
