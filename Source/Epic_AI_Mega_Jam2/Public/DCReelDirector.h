// Highlight reel (R13), local on every machine. For the clip the host is showing, it rebuilds that sector's city
// from the run seed, replays the recorded collapses and blasts on a remapped clock, walks a mannequin puppet along
// the photographer's 2-second pose buffer and films it from a slow third-person orbit. The host drives slow motion
// and the freeze through global time dilation; at the freeze the camera cuts to the photographer's exact view.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCReelDirector.generated.h"

class ADCBuilding;
class ADCOrbitalStrike;
class UCameraComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCReelDirector : public AActor
{
	GENERATED_BODY()

public:
	ADCReelDirector();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FPendingCollapse
	{
		float At = 0.f;
		TWeakObjectPtr<ADCBuilding> Building;
		FVector Epicenter = FVector::ZeroVector;
	};

	void Restage(int32 ClipIndex);
	void ClearStage();
	void TickPuppet(float ClipTime);
	void TickCamera(float ClipTime, float Progress);
	void SetPawnsHidden(bool bHide);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Puppet;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PuppetCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PuppetFlash;
	UPROPERTY() TArray<TObjectPtr<AActor>> Staged;
	UPROPERTY() TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim;

	TArray<FPendingCollapse> Pending;
	int32 StagedIndex = -1;
	float WindowStart = 0.f;
	float ShotTime = 0.f;
	bool bRunning = false;
	bool bAnimSet = false;
};
