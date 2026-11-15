// The photographer-soldier: third person over the shoulder with three slots (1 rifle, 2 stratagem call-in,
// 3 camera). Raising the camera switches to a first-person viewfinder. Clients send intent through RPCs; the host
// validates film, cooldowns and points and owns health.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DCTypes.h"
#include "DCPhotoProbe.h"
#include "DCCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UInputComponent;
struct FInputActionInstance;

/** Local-only feedback the HUD reads (never replicated). */
struct FDCLocalFeedback
{
	float HitMarkerTime = -10.f;
	bool bLastHitKill = false;
	float HurtTime = -10.f;
	FVector HurtFrom = FVector::ZeroVector;
	float ShutterTime = -10.f;
	FString Toast;
	float ToastTime = -10.f;
};

struct FPostProcessSettings;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADCCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	/** Binds the input actions again (after a key rebind rebuilt the mapping, R24). */
	void RebindInput()
	{
		DestroyPlayerInputComponent();
		PawnClientRestart();
	}
	/** Voxel city meshes are built locally on every machine and are not net-addressable: never a movement base (the
	 *  city never moves), so co-op movement replication carries no unresolvable component (CP4 review). */
	virtual void SetBase(FMovementBaseInterfaceData* MovementBaseInterfaceData, const FName BoneName = NAME_None, bool bNotifyActor = true) override;
	using ACharacter::SetBase;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// --- State the HUD and enemies read ---
	EDCSlot GetSlot() const { return Slot; }
	bool IsCameraRaised() const { return bCameraRaised; }
	/** R16: aiming the gun (replicated, so every machine shows the aim pose). */
	bool IsRifleAiming() const { return bRifleAim; }
	/** Aim pitch (degrees, the control pitch; replicated from the owner) for the aim pose on every machine. */
	float GetAimPitch() const { return AimPitch; }
	/** The rifle loop runs (the draw is over): the aim pose may bend the spine. */
	bool IsRifleLocoActive() const { return GetWorld() && GetWorld()->GetTimeSeconds() >= LocoBlockedUntil; }
	float GetDownedAt() const { return DownedAt; }
	bool IsDowned() const { return bDowned; }
	bool HasLastFrame() const { return bDowned && bLastFrameReady; }
	/** Last Frame in co-op (R21): slow motion on the dying player's own machine only, as presentation
	 *  (UDCPresentationClock: fx and animation slow, simulation stays real time). Same on a listen host and a client. */
	UFUNCTION(Client, Reliable) void ClientLastFrameSlowMo(float Dilation, float Seconds);
	bool IsDrawing() const;
	bool IsReloading() const;
	bool IsSpotlit() const;
	float GetHealth() const { return Health; }
	int32 GetAmmo() const { return Ammo; }
	int32 GetReserve() const { return Reserve; }
	/** R8: the weapon in the gun slot (index into UDCTuning::Weapons) and its row. */
	int32 GetWeaponIndex() const { return WeaponIndex; }
	const struct FDCWeaponDef& ActiveWeapon() const;
	float GetZoomFOV() const { return LensFOV(); }
	/** Viewfinder FOV: the wheel zoom plus lens gear (Fisheye ExtraFOV). */
	float LensFOV() const;
	/** Photo exposure (bloom, stop-down while the flash blinds, spotlight blow-out) for any photo capture. */
	static void ApplyPhotoLook(const UWorld* World, FPostProcessSettings& PhotoLook, bool bSpotlit);
	float GetWindOnLeft() const;
	FName GetSelectedStratagem() const { return SelectedStratagem; }
	bool GetStrikeAim(FVector& Out) const { Out = StrikeAimPoint; return bStrikeAimValid; }
	FDCShotView GetShotView() const;
	float GetReviveProgress() const { return ReviveProgress; }
	/** Solo self-revive still possible (one left this sector and the window has not closed). */
	bool CanSelfRevive() const;
	/** Seconds left in the solo self-revive window (0 when none). */
	float SelfReviveSecondsLeft() const;
	const FDCLocalFeedback& Feedback() const { return LocalFeedback; }
	void Toast(const FString& Text);

	// --- Host API ---
	void HostApplyHurt(float Amount, const FVector& From);
	void HostSetSpotlit(float Until) { SpotlitUntil = Until; }
	void HostRevive();
	void HostRefill();
	void HostResupply(int32 Film, int32 Ammo, float Heal);
	void HostToast(const FString& Text) { ClientToast(Text); }
	void HostCue(FName Cue) { ClientCue(Cue); }
	/** Host: the recorded poses between From and To (server time), for the reel. */
	void CopyPoses(float From, float To, TArray<FDCPoseSample>& Out) const;

	/** Camera shake from blasts (local). */
	void AddTrauma(float Amount) { Trauma = FMath::Min(Trauma + Amount, 1.5f); }
	/** R17: hit shake channel (unit hits), capped and rate-limited by FDCCameraTuning; never stacks onto blast shake. */
	float HitTrauma = 0.f;
	float LastHitShakeTime = -100.f;
	int32 ShakeLogHits = 0;
	int32 ShakeLogFlinches = 0;
	float ShakeLogMax = 0.f;
	float ShakeLogNext = 0.f;

private:
	// Input handlers (bound by action name from the input table).
	void OnMove(const FInputActionInstance& I);
	void OnLook(const FInputActionInstance& I);
	void OnJump(const FInputActionInstance& I);
	void OnJumpEnd(const FInputActionInstance& I);
	void OnSprint(const FInputActionInstance& I);
	void OnSprintEnd(const FInputActionInstance& I);
	void OnCrouch(const FInputActionInstance& I);
	void OnCrouchEnd(const FInputActionInstance& I);
	void OnFire(const FInputActionInstance& I);
	void OnFireEnd(const FInputActionInstance& I);
	void OnAim(const FInputActionInstance& I);
	void OnAimEnd(const FInputActionInstance& I);
	void OnReload(const FInputActionInstance& I);
	void OnSlot1(const FInputActionInstance& I);
	void OnSlot2(const FInputActionInstance& I);
	void OnSlot3(const FInputActionInstance& I);
	void OnZoom(const FInputActionInstance& I);
	void OnCycle(const FInputActionInstance& I);
	void OnInteract(const FInputActionInstance& I);
	void OnInteractEnd(const FInputActionInstance& I);

	// Slot primary actions, dispatched through a slot table.
	void PrimaryRifle();
	void PrimaryStratagem();
	void PrimaryCamera();

	void SelectSlot(EDCSlot NewSlot);
	// R8 weapons (DCWeapons.cpp): per-weapon magazines, the switch, the forge model on the gun prop.
	void InitWeapons();
	void SelectWeapon(int32 Index);
	void NextWeapon();
	void NextStratagem();
	void ApplyWeaponModel();
	UFUNCTION(Server, Reliable) void ServerSetWeapon(uint8 Index);
	UFUNCTION() void OnRep_Weapon();
	void FireRifleOnce();
	void DoReload();
	void PlayRifleFx(const FVector& Start, const FVector& End);
	void TakePhoto();
	void UpdateStrikeAim();
	void UpdateMoveSpeed();
	void UpdateView(float DeltaSeconds);
	void UpdateHeldItems(float DeltaSeconds);
	void UpdateFootsteps(float DeltaSeconds);
	void UpdateRevive(float DeltaSeconds);
	void RecordPose();
	bool InSector() const;
	/** The local reel director while the reel plays (second take input goes there), else null. */
	class ADCReelDirector* ReelDirector() const;
	/** Measures the shot and applies the photographer gear and the city mutators to the inputs. */
	FDCPhotoInputs MeasureShot(const FDCShotView& View, float ShotTime, int32& OutBlastId) const;
	void ApplyStrobe(const FDCShotView& View);
	float Now() const;

	UFUNCTION(Server, Reliable) void ServerSetSlot(EDCSlot NewSlot);
	UFUNCTION(Server, Reliable) void ServerSetCameraRaised(bool bRaised);
	UFUNCTION(Server, Reliable) void ServerSetRifleAim(bool bAim);
	UFUNCTION(Server, Unreliable) void ServerAimPitch(float Pitch);
	void SetRifleAim(bool bAim);
	float SentAimPitch = 0.f;
	float NextAimPitchSend = 0.f;
	float AimBlend = 0.f;
	/** R16: how far the held rifle points at the aim point instead of along the hand (every machine, cosmetic). */
	float GunAimBlend = 0.f;
	float GunShotUntil = 0.f;
	/** R19 fall net (host). */
	void UpdateFallNet();
	FVector LastSafeSpot = FVector::ZeroVector;
	bool bHasSafeSpot = false;
	float NextSafeSample = 0.f;
	UFUNCTION(Server, Reliable) void ServerSetSprint(bool bSprint);
	UFUNCTION(Server, Unreliable) void ServerFire(FVector_NetQuantize Start, FVector_NetQuantizeNormal Dir);
	UFUNCTION(Server, Reliable) void ServerReload();
	UFUNCTION(Server, Reliable) void ServerCallStrike(FName StratagemId, FVector_NetQuantize Target);
	UFUNCTION(Server, Reliable) void ServerSubmitPhoto(int32 PhotoId, FVector_NetQuantize10 CamLoc, FRotator CamRot, float FOV, float ShotTime);
	/** Host grade of an accepted shot at its own shot time; waits a frame at a time while a voxel op is still on the
	 *  worker (until GiveUpAt, real time), so a shutter right after a strike sees the towers it felled (C3.1). */
	void HostGradeShot(int32 PhotoId, FVector CamLoc, FRotator CamRot, float FOV, float When, bool bLast, double GiveUpAt);
	UFUNCTION(Server, Reliable) void ServerSetReviving(bool bHolding);
	UFUNCTION(Client, Reliable) void ClientHitConfirm(bool bKill);
	UFUNCTION(Client, Reliable) void ClientHurt(FVector_NetQuantize From);
	UFUNCTION(Client, Reliable) void ClientPhotoGraded(int32 PhotoId, FDCPhotoGrade Grade);
	UFUNCTION(Client, Reliable) void ClientToast(const FString& Text);
	UFUNCTION(Client, Reliable) void ClientCue(FName Cue);
	UFUNCTION(Client, Reliable) void ClientSyncAmmo(int32 NewAmmo, int32 NewReserve, uint8 ForWeapon);
	UFUNCTION(NetMulticast, Unreliable) void MulticastRifleFx(FVector_NetQuantize Start, FVector_NetQuantize End);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShutterFx();
	UFUNCTION() void OnRep_Slot();
	UFUNCTION() void OnRep_Downed();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> ThirdCam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> FirstCam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> RifleMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PhotoCamMesh;
	/** The forge-made press camera on the chest (voxel model), hidden while the camera is in hand. */
	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> ChestCam;
	/** R23: the worn outfit parts, rebuilt when the player state's share code changes. */
	UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> OutfitMeshes;
	FString AppliedOutfit;
	bool bOutfitApplied = false;
	void TickOutfit();
	void HideBodyFromOwner(bool bHide);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeaconMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> RimLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> StrikeMarker;
	float StrikeMarkerRadius = -1.f;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Tracer;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> MuzzleFlash;
	/** R10 rifle locomotion (cosmetic, every machine): clips preloaded at BeginPlay, the one playing now, and the end of
	 *  the draw motion it waits for. */
	UPROPERTY() TArray<TObjectPtr<UAnimSequenceBase>> RifleJogLoaded;
	UPROPERTY() TArray<TObjectPtr<UAnimSequenceBase>> RifleWalkLoaded;
	UPROPERTY() TObjectPtr<UAnimSequenceBase> RifleIdleLoaded;
	UPROPERTY() TObjectPtr<UAnimSequenceBase> LocoClip;
	float LocoBlockedUntil = 0.f;
	void UpdateRifleLoco();

	UPROPERTY(ReplicatedUsing = OnRep_Slot) EDCSlot Slot = EDCSlot::Rifle;
	/** R8: the weapon in the gun slot, replicated so everyone sees its model and tracer. */
	UPROPERTY(ReplicatedUsing = OnRep_Weapon) uint8 WeaponIndex = 0;
	/** The host's own copy of the owner's weapon (a listen host's local switch must not move the host counters). */
	uint8 HostWeapon = 0;
	/** Magazine (X) and reserve (Y) of every weapon not in hand: owner-local and host copies. */
	TArray<FIntPoint> WeaponStash;
	TArray<FIntPoint> HostWeaponStash;
	/** The forge model of the weapon in hand, riding on RifleMesh. */
	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> GunVox;
	/** The slot whose draw motion last played on this machine. */
	EDCSlot DrawnSlot = EDCSlot::Rifle;
	UPROPERTY(Replicated) bool bCameraRaised = false;
	UPROPERTY(Replicated) bool bRifleAim = false;
	UPROPERTY(Replicated) float AimPitch = 0.f;
	UPROPERTY(Replicated) bool bSprinting = false;
	UPROPERTY(Replicated) float Health = 100.f;
	/** Host clock of the last damage taken (health regen waits RegenDelaySeconds after it). */
	float HostHurtAt = -100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Downed) bool bDowned = false;
	/** Last Frame (R21): a downed player keeps one shutter, spent or lost on revive. */
	UPROPERTY(Replicated) bool bLastFrameReady = false;
	/** Enemies hidden from one Last Frame capture (at the lens), shown again right after it. */
	TArray<TWeakObjectPtr<AActor>> LensHidden;
	/** Landing spring (local camera feel): offset in cm and its velocity. */
	float LandDip = 0.f;
	float LandDipVel = 0.f;
	UPROPERTY(Replicated) float SpotlitUntil = -10.f;
	UPROPERTY(Replicated) float ReviveProgress = 0.f;
	UPROPERTY(Replicated) int32 SelfRevivesLeft = 1;
	/** Server time this player went down. */
	UPROPERTY(Replicated) float DownedAt = 0.f;

	// Owner-local weapon and camera state (the host keeps its own counters for validation).
	int32 Ammo = 30;
	int32 Reserve = 180;
	int32 HostAmmo = 30;
	int32 HostReserve = 180;
	float ReloadEndTime = -1.f;
	float NextFireTime = 0.f;
	float HostNextFireTime = 0.f;
	float DrawEndTime = 0.f;
	float NextShutterTime = 0.f;
	float HostNextShutterTime = 0.f;
	float ZoomFOV = 50.f;
	float RecoilPitch = 0.f;
	float Trauma = 0.f;
	float ViewBlend = 0.f;
	float StepDistance = 0.f;
	float LastLowAmmoCue = -10.f;
	float TracerHideTime = 0.f;
	float MuzzleHideTime = 0.f;
	bool bTriggerHeld = false;
	bool bAimHeld = false;
	bool bInteractHeld = false;
	bool bHostReviveHeld = false;
	bool bStrikeAimValid = false;
	FVector StrikeAimPoint = FVector::ZeroVector;
	FName SelectedStratagem;
	int32 PhotoCounter = 0;
	FDCLocalFeedback LocalFeedback;
	/** Host-only ring of recent poses (a few seconds) for the reel re-staging. */
	TArray<FDCPoseSample> PoseRing;
	float NextPoseTime = 0.f;
};
