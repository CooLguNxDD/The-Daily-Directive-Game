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

	// Second take (R16): while a clip replays, the local player raises a camera from a free angle and snaps.
	void SetTakeRaised(bool bRaised);
	void SnapSecondTake();
	bool IsTakeRaised() const { return bTakeRaised; }
	bool CanTakeThisClip() const;
	/** The host refused a take of this clip: the clip is free to shoot again. */
	void OnTakeRejected(int32 ClipIndex) { TakenClip = TakenClip == ClipIndex ? -1 : TakenClip; }
	/** The moment of the run being replayed now, on the original server clock. */
	float ContentTime() const;
	int32 GetStagedIndex() const { return StagedIndex; }

private:
	struct FPendingCollapse
	{
		float At = 0.f;
		TWeakObjectPtr<ADCBuilding> Building;
		FVector Epicenter = FVector::ZeroVector;
		float ContentTime = 0.f;
	};

	void Restage(int32 ClipIndex);
	void ClearStage();
	void TickPuppet(float ClipTime);
	void TickCamera(float ClipTime, float Progress);
	void SetPawnsHidden(bool bHide);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USceneCaptureComponent2D> TakeCapture;
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
	bool bTakeRaised = false;
	int32 TakenClip = -1;
	int32 TakeCounter = 0;
};
