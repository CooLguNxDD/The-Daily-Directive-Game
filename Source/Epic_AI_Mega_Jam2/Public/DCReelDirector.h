// Highlight reel (R13), local on every machine. For the clip the host is showing, it rebuilds that sector's city
// from the run seed, replays the recorded collapses and blasts on a remapped clock, walks a mannequin puppet along
// the photographer's 2-second pose buffer and films it from a slow third-person orbit. The host drives slow motion
// and the freeze through global time dilation; at the freeze the camera cuts to the photographer's exact view.
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelTypes.h"
#include "DCTypes.h"
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
	bool IsSlating() const { return SlateFramesLeft >= 0 || FPlatformTime::Seconds() < SlateUntilReal; }
	ADCReelDirector();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Second take (R16): while a clip replays, the local player raises a camera from a free angle and snaps.
	void SetTakeRaised(bool bRaised);
	void SnapSecondTake();
	/** Second take: move the raised camera (input axes, real seconds) and zoom it, within the take radius. */
	void DollyTake(const FVector2D& Axes);
	void ZoomTake(float Wheel);
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
		float At = 0.f; // content time of the collapse (clip time), not server time
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
	/** Restage slate: frames left before the restage runs (-1 = none pending) and when it lifts (real seconds). */
	int32 SlateFramesLeft = -1;
	double SlateUntilReal = 0.0;
	UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> PuppetParts;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PuppetCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PuppetFlash;
	UPROPERTY() TArray<TObjectPtr<AActor>> Staged;
	UPROPERTY() TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim;

	TArray<FPendingCollapse> Pending;
	/** Voxel city: the clip sector's ops that happen during the clip, at their replay server time. */
	TArray<TPair<float, FDCVoxelOp>> PendingVoxelOps;
	/** Async restage (Tuning.bAsyncReelRestage): the slate stays up while the city crafts and these ops (the clip
	 *  sector's ops before its window) replay on the worker; then the clip plays. */
	TArray<FDCVoxelOp> PreWindowOps;
	bool bRestaging = false;
	bool bPreWindowQueued = false;
	/** Restaged; waiting for the host's clip clock (GS->bReelClipLive) before the slate lifts and the strikes spawn. */
	bool bAwaitLive = false;
	TArray<FDCBlastRecord> PendingBlasts;
	int32 StagedIndex = -1;
	float WindowStart = 0.f;
	float ShotTime = 0.f;
	bool bRunning = false;
	bool bAnimSet = false;
	bool bTakeRaised = false;
	int32 TakenClip = -1;
	int32 TakeCounter = 0;
	/** Second take camera: position and lens while raised. */
	FVector TakeLocation = FVector::ZeroVector;
	float TakeFOV = 60.f;
};
