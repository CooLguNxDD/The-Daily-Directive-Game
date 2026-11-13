#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DCPhotoProbe.h"
#include "DCTypes.h"
#include "DCPlayerController.generated.h"

class UDCInputSet;
class UTextureRenderTarget2D;

/** A photo as the photographer's machine holds it: the captured image plus its grade. */
USTRUCT()
struct FDCLocalPhoto
{
	GENERATED_BODY()
	UPROPERTY() int32 PhotoId = 0;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Image;
	UPROPERTY() FDCPhotoGrade Grade;
	UPROPERTY() bool bConfirmed = false;
	FDCShotView View;
	float ShotTime = 0.f;
	double TakenReal = 0.0;
};

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADCPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UDCInputSet* GetInputSet();
	float GetSensitivity() const { return Sensitivity; }
	void SetSensitivity(float Value);

	// Local photo album (images never leave this machine).
	void AddLocalPhoto(int32 PhotoId, UTextureRenderTarget2D* Image, const FDCPhotoGrade& Preview, const FDCShotView& View, float ShotTime);
	void ConfirmLocalPhoto(int32 PhotoId, const FDCPhotoGrade& Grade);
	const FDCLocalPhoto* FindLocalPhoto(int32 PhotoId) const;
	const FDCLocalPhoto* GetLastPhoto() const { return Album.Num() ? &Album.Last() : nullptr; }
	const TArray<FDCLocalPhoto>& GetAlbum() const { return Album; }

private:
	void OnSensUp();
	void OnSensDown();

	UPROPERTY() TObjectPtr<UDCInputSet> InputSet;
	UPROPERTY() TArray<FDCLocalPhoto> Album;
	float Sensitivity = 1.f;
};
