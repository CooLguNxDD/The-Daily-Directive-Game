// The builder's live turntable (R23): a mannequin wearing the outfit in front of a paper backdrop, captured to a render
// target the menu shows, turning slowly. Spawned high above the city so nothing else is in shot (show-only capture).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/DCOutfit.h"
#include "DCOutfitPreview.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UProceduralMeshComponent;
class UStaticMeshComponent;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCOutfitPreview : public AActor
{
	GENERATED_BODY()
public:
	ADCOutfitPreview();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void Show(const FDCOutfit& Outfit);
	/** Evidence frames (R18): holds the turntable at Yaw (degrees, 0 = facing the camera); a negative value spins again. */
	void Face(float Yaw);
	UTextureRenderTarget2D* GetImage() const { return Image; }
	/** 0 until the capture has had a moment to light the scene, then 1 (the first captures came out black, V5.5). */
	float ImageAlpha() const;
	/** R5: renders the ID card (turntable angles of this outfit + call sign + share code) to a PNG; returns its path
	 *  (empty on failure). Synchronous: a one-off hitch on the button press. */
	FString ExportCard(const FString& CallSign, const FString& ShareCode);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Turn;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Backdrop;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Image;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Parts;
	double StartReal = 0.0;
	bool bHold = false;
};
