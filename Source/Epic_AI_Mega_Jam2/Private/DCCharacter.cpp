#include "DCCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCEnemy.h"
#include "DCEnvironment.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCInput.h"
#include "DCOrbitalStrike.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCReelDirector.h"
#include "DCTuning.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float ReviveRangeCm = 300.f;
	constexpr float TeamReviveSeconds = 3.f;
	constexpr float SelfReviveSeconds = 4.f;
	constexpr float MaxStrikeRangeCm = 12000.f;
	constexpr float ShotClockToleranceSeconds = 1.f;

	/** Input rows: action name -> trigger event -> handler. A new control is a new row. */
	struct FBindRow
	{
		const TCHAR* Action;
		ETriggerEvent Event;
		void (ADCCharacter::*Fn)(const FInputActionInstance&);
	};
}

ADCCharacter::ADCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	SetNetUpdateFrequency(60.f);

	GetCapsuleComponent()->InitCapsuleSize(40.f, 92.f);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));
	BaseEyeHeight = 70.f;
	CrouchedEyeHeight = 40.f;

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(RootComponent);
	Boom->bUsePawnControlRotation = true;
	Boom->bDoCollisionTest = true;
	Boom->SetRelativeLocation(FVector(0, 0, 40.f));

	ThirdCam = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdCam"));
	ThirdCam->SetupAttachment(Boom, USpringArmComponent::SocketName);

	FirstCam = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstCam"));
	FirstCam->SetupAttachment(RootComponent);
	FirstCam->SetRelativeLocation(FVector(18.f, 0.f, 70.f));
	FirstCam->bUsePawnControlRotation = true;
	FirstCam->SetActive(false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(FirstCam);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	auto MakeProp = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* M = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		M->SetupAttachment(GetMesh());
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(true);
		return M;
	};
	RifleMesh = MakeProp(TEXT("Rifle"));
	PhotoCamMesh = MakeProp(TEXT("PhotoCam"));
	BeaconMesh = MakeProp(TEXT("Beacon"));

	StrikeMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StrikeMarker"));
	StrikeMarker->SetupAttachment(RootComponent);
	StrikeMarker->SetUsingAbsoluteLocation(true);
	StrikeMarker->SetUsingAbsoluteRotation(true);
	StrikeMarker->SetUsingAbsoluteScale(true);
	StrikeMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StrikeMarker->SetOnlyOwnerSee(true);
	StrikeMarker->SetCastShadow(false);
	StrikeMarker->SetVisibility(false);

	Tracer = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tracer"));
	Tracer->SetupAttachment(RootComponent);
	Tracer->SetUsingAbsoluteLocation(true);
	Tracer->SetUsingAbsoluteRotation(true);
	Tracer->SetUsingAbsoluteScale(true);
	Tracer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tracer->SetCastShadow(false);
	Tracer->SetVisibility(false);
}

void ADCCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCCharacter, Slot);
	DOREPLIFETIME(ADCCharacter, bCameraRaised);
	DOREPLIFETIME(ADCCharacter, bSprinting);
	DOREPLIFETIME(ADCCharacter, Health);
	DOREPLIFETIME(ADCCharacter, bDowned);
	DOREPLIFETIME(ADCCharacter, SpotlitUntil);
	DOREPLIFETIME(ADCCharacter, ReviveProgress);
	DOREPLIFETIME(ADCCharacter, SelfRevivesLeft);
	DOREPLIFETIME(ADCCharacter, DownedAt);
}

void ADCCharacter::BeginPlay()
{
	Super::BeginPlay();
	const UDCTuning& T = UDCTuning::Get();

	if (USkeletalMesh* Mannequin = Cast<USkeletalMesh>(T.Assets.PlayerMesh.TryLoad()))
	{
		GetMesh()->SetSkeletalMesh(Mannequin);
		if (UClass* Anim = T.Assets.PlayerAnimClass.TryLoadClass<UAnimInstance>())
		{
			GetMesh()->SetAnimInstanceClass(Anim);
		}
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxAcceleration = T.Move.MaxAcceleration;
	Move->BrakingDecelerationWalking = T.Move.BrakingDeceleration;
	Move->GroundFriction = T.Move.GroundFriction;
	Move->JumpZVelocity = T.Move.JumpZVelocity;
	Move->AirControl = T.Move.AirControl;
	Move->GravityScale = T.Move.GravityScale;
	Move->MaxWalkSpeedCrouched = T.Move.CrouchSpeed;
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFriction = T.Move.GroundFriction;
	UpdateMoveSpeed();

	Boom->TargetArmLength = T.Move.ArmLength;
	Boom->SocketOffset = T.Move.ShoulderOffset;
	ThirdCam->SetFieldOfView(T.Move.ThirdPersonFOV);
	ZoomFOV = (T.Camera.ViewfinderFOVWide + T.Camera.ViewfinderFOVTight) * 0.5f;

	// Viewfinder tunnel: heavy vignette and a slight contrast push only while looking through the camera.
	FPostProcessSettings& PP = FirstCam->PostProcessSettings;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = T.Camera.VignetteIntensity;
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.15f);
	FirstCam->PostProcessBlendWeight = 1.f;

	const FDCLookTuning& Look = T.Look;
	const FName Hand = T.Assets.HandSocket;
	RifleMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	RifleMesh->SetStaticMesh(DCAssets::Cube());
	RifleMesh->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.08f, 0.08f, 0.07f)));
	RifleMesh->SetRelativeLocation(FVector(-10.f, 30.f, 0.f));
	RifleMesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	PhotoCamMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	PhotoCamMesh->SetStaticMesh(DCAssets::Cube());
	PhotoCamMesh->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.9f, 0.85f, 0.75f)));
	BeaconMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	BeaconMesh->SetStaticMesh(DCAssets::Cylinder());
	BeaconMesh->SetMaterial(0, DCAssets::MakeFlat(this, Look.Accent, 12.f));

	StrikeMarker->SetStaticMesh(DCAssets::Cylinder());
	StrikeMarker->SetMaterial(0, DCAssets::MakeFlat(this, Look.Accent, 6.f));
	Tracer->SetStaticMesh(DCAssets::Cube());
	Tracer->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(1.f, 0.8f, 0.4f), 60.f));

	Ammo = HostAmmo = T.Rifle.MagazineSize;
	Reserve = HostReserve = T.Rifle.StartReserve;
	if (HasAuthority())
	{
		Health = T.Move.MaxHealth;
	}
	if (T.Stratagems.Num() > 0)
	{
		SelectedStratagem = T.Stratagems[0].Id;
	}
	OnRep_Slot();
}

float ADCCharacter::Now() const
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return GS ? GS->Now() : GetWorld()->GetTimeSeconds();
}

bool ADCCharacter::IsDrawing() const { return Now() < DrawEndTime; }
bool ADCCharacter::IsReloading() const { return ReloadEndTime > 0.f; }
bool ADCCharacter::IsSpotlit() const { return SpotlitUntil > Now(); }
float ADCCharacter::GetWindOnLeft() const { return FMath::Max(0.f, NextShutterTime - Now()); }

void ADCCharacter::Toast(const FString& Text)
{
	LocalFeedback.Toast = Text;
	LocalFeedback.ToastTime = GetWorld()->GetRealTimeSeconds();
}

FDCShotView ADCCharacter::GetShotView() const
{
	FDCShotView View;
	View.Location = FirstCam->GetComponentLocation();
	View.Rotation = GetControlRotation();
	View.FOV = ZoomFOV;
	return View;
}

// ---------------------------------------------------------------- input

void ADCCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const UDCInputSet* Set = UDCInputSet::Get(GetController());
	if (!Input || !Set)
	{
		return;
	}
	static const FBindRow Rows[] = {
		{TEXT("Move"), ETriggerEvent::Triggered, &ADCCharacter::OnMove},
		{TEXT("Look"), ETriggerEvent::Triggered, &ADCCharacter::OnLook},
		{TEXT("Jump"), ETriggerEvent::Started, &ADCCharacter::OnJump},
		{TEXT("Jump"), ETriggerEvent::Completed, &ADCCharacter::OnJumpEnd},
		{TEXT("Sprint"), ETriggerEvent::Started, &ADCCharacter::OnSprint},
		{TEXT("Sprint"), ETriggerEvent::Completed, &ADCCharacter::OnSprintEnd},
		{TEXT("Crouch"), ETriggerEvent::Started, &ADCCharacter::OnCrouch},
		{TEXT("Crouch"), ETriggerEvent::Completed, &ADCCharacter::OnCrouchEnd},
		{TEXT("Fire"), ETriggerEvent::Started, &ADCCharacter::OnFire},
		{TEXT("Fire"), ETriggerEvent::Completed, &ADCCharacter::OnFireEnd},
		{TEXT("Aim"), ETriggerEvent::Started, &ADCCharacter::OnAim},
		{TEXT("Aim"), ETriggerEvent::Completed, &ADCCharacter::OnAimEnd},
		{TEXT("Reload"), ETriggerEvent::Started, &ADCCharacter::OnReload},
		{TEXT("Slot1"), ETriggerEvent::Started, &ADCCharacter::OnSlot1},
		{TEXT("Slot2"), ETriggerEvent::Started, &ADCCharacter::OnSlot2},
		{TEXT("Slot3"), ETriggerEvent::Started, &ADCCharacter::OnSlot3},
		{TEXT("Zoom"), ETriggerEvent::Triggered, &ADCCharacter::OnZoom},
		{TEXT("Cycle"), ETriggerEvent::Started, &ADCCharacter::OnCycle},
		{TEXT("Interact"), ETriggerEvent::Started, &ADCCharacter::OnInteract},
		{TEXT("Interact"), ETriggerEvent::Completed, &ADCCharacter::OnInteractEnd},
	};
	for (const FBindRow& Row : Rows)
	{
		if (const UInputAction* Action = Set->Find(Row.Action))
		{
			Input->BindAction(Action, Row.Event, this, Row.Fn);
		}
	}
}

void ADCCharacter::OnMove(const FInputActionInstance& I)
{
	const FVector2D V = I.GetValue().Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void ADCCharacter::OnLook(const FInputActionInstance& I)
{
	const FVector2D V = I.GetValue().Get<FVector2D>();
	const ADCPlayerController* PC = Cast<ADCPlayerController>(GetController());
	// Narrower viewfinder FOV slows the look so framing stays precise.
	const float FovScale = bCameraRaised ? ZoomFOV / UDCTuning::Get().Move.ThirdPersonFOV : 1.f;
	const float Sens = (PC ? PC->GetSensitivity() : 1.f) * FovScale;
	AddControllerYawInput(V.X * Sens);
	AddControllerPitchInput(V.Y * Sens);
}

void ADCCharacter::OnJump(const FInputActionInstance&)
{
	if (!bDowned && CanJump())
	{
		DCAudio::PlayAt(this, TEXT("Jump"), GetActorLocation() - FVector(0, 0, 80.f), 0.5f, FMath::FRandRange(0.9f, 1.1f));
	}
	if (!bDowned)
	{
		Jump();
	}
}
void ADCCharacter::OnJumpEnd(const FInputActionInstance&) { StopJumping(); }

void ADCCharacter::OnSprint(const FInputActionInstance&)
{
	bSprinting = true;
	UpdateMoveSpeed();
	ServerSetSprint(true);
}

void ADCCharacter::OnSprintEnd(const FInputActionInstance&)
{
	bSprinting = false;
	UpdateMoveSpeed();
	ServerSetSprint(false);
}

void ADCCharacter::OnCrouch(const FInputActionInstance&)
{
	DCAudio::PlayAt(this, TEXT("Footstep"), GetActorLocation() - FVector(0, 0, 80.f), 0.25f, 0.7f); // gear rustle
	Crouch();
}
void ADCCharacter::OnCrouchEnd(const FInputActionInstance&) { UnCrouch(); }

float ADCCharacter::SelfReviveSecondsLeft() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const bool bSolo = GS && GS->PlayerArray.Num() <= 1;
	if (!bDowned || !bSolo || SelfRevivesLeft <= 0)
	{
		return 0.f;
	}
	return FMath::Max(0.f, DownedAt + UDCTuning::Get().Run.SelfReviveWindowSeconds - Now());
}

bool ADCCharacter::CanSelfRevive() const
{
	return SelfReviveSecondsLeft() > 0.f;
}

bool ADCCharacter::InSector() const
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return GS && GS->Phase == EDCRunPhase::Sector;
}

ADCReelDirector* ADCCharacter::ReelDirector() const
{
	const ADCPlayerController* PC = Cast<ADCPlayerController>(GetController());
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return PC && GS && GS->Phase == EDCRunPhase::Reel ? PC->GetReelDirector() : nullptr;
}

void ADCCharacter::OnFire(const FInputActionInstance&)
{
	bTriggerHeld = true;
	if (ADCReelDirector* Director = ReelDirector())
	{
		Director->SnapSecondTake(); // R16: second take during the reel
		return;
	}
	if (bDowned || IsDrawing() || !InSector())
	{
		return;
	}
	using FPrimary = void (ADCCharacter::*)();
	static const TMap<EDCSlot, FPrimary> Primary = {
		{EDCSlot::Rifle, &ADCCharacter::PrimaryRifle},
		{EDCSlot::Stratagem, &ADCCharacter::PrimaryStratagem},
		{EDCSlot::Camera, &ADCCharacter::PrimaryCamera},
	};
	if (const FPrimary* Fn = Primary.Find(Slot))
	{
		(this->**Fn)();
	}
}

void ADCCharacter::OnFireEnd(const FInputActionInstance&) { bTriggerHeld = false; }

void ADCCharacter::OnAim(const FInputActionInstance&)
{
	bAimHeld = true;
	if (ADCReelDirector* Director = ReelDirector())
	{
		Director->SetTakeRaised(true);
		return;
	}
	if (Slot != EDCSlot::Camera || bDowned || !InSector())
	{
		return;
	}
	bCameraRaised = true;
	UpdateMoveSpeed();
	ServerSetCameraRaised(true);
	DCAudio::Play2D(this, TEXT("Draw"), 0.6f, 1.3f);
}

void ADCCharacter::OnAimEnd(const FInputActionInstance&)
{
	bAimHeld = false;
	if (ADCReelDirector* Director = ReelDirector())
	{
		Director->SetTakeRaised(false);
	}
	if (!bCameraRaised)
	{
		return;
	}
	bCameraRaised = false;
	UpdateMoveSpeed();
	ServerSetCameraRaised(false);
}

void ADCCharacter::OnReload(const FInputActionInstance&) { DoReload(); }

void ADCCharacter::DoReload()
{
	const FDCRifleTuning& R = UDCTuning::Get().Rifle;
	if (Slot != EDCSlot::Rifle || IsReloading() || Reserve <= 0 || Ammo >= R.MagazineSize || bDowned)
	{
		return;
	}
	ReloadEndTime = Now() + R.ReloadSeconds;
	DCAudio::PlayAt(this, TEXT("Reload"), GetActorLocation());
	ServerReload();
}

void ADCCharacter::OnSlot1(const FInputActionInstance&) { SelectSlot(EDCSlot::Rifle); }
void ADCCharacter::OnSlot2(const FInputActionInstance&) { SelectSlot(EDCSlot::Stratagem); }
void ADCCharacter::OnSlot3(const FInputActionInstance&) { SelectSlot(EDCSlot::Camera); }

void ADCCharacter::OnZoom(const FInputActionInstance& I)
{
	const float Wheel = I.GetValue().Get<float>();
	if (Slot == EDCSlot::Stratagem && !bCameraRaised)
	{
		OnCycle(I);
		return;
	}
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const float Wide = C.ViewfinderFOVWide + (PS ? PS->ExtraFOV : 0.f);
	const float Before = ZoomFOV;
	ZoomFOV = FMath::Clamp(ZoomFOV - Wheel * C.ZoomStep, C.ViewfinderFOVTight, Wide);
	if (!FMath::IsNearlyEqual(Before, ZoomFOV))
	{
		DCAudio::Play2D(this, TEXT("UIClick"), 0.25f, ZoomFOV < Before ? 1.3f : 0.9f); // lens ring tick
	}
}

void ADCCharacter::OnCycle(const FInputActionInstance&)
{
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	if (!PS || PS->OwnedStratagems.Num() == 0)
	{
		return;
	}
	const int32 Index = PS->OwnedStratagems.IndexOfByKey(SelectedStratagem);
	SelectedStratagem = PS->OwnedStratagems[(Index + 1) % PS->OwnedStratagems.Num()];
	DCAudio::Play2D(this, TEXT("UIClick"));
}

void ADCCharacter::OnInteract(const FInputActionInstance&)
{
	bInteractHeld = true;
	ServerSetReviving(true);
}

void ADCCharacter::OnInteractEnd(const FInputActionInstance&)
{
	bInteractHeld = false;
	ServerSetReviving(false);
}

// ---------------------------------------------------------------- slots

void ADCCharacter::SelectSlot(EDCSlot NewSlot)
{
	if (NewSlot == Slot || bDowned)
	{
		return;
	}
	if (bCameraRaised)
	{
		bCameraRaised = false;
		ServerSetCameraRaised(false);
	}
	Slot = NewSlot;
	ReloadEndTime = -1.f;
	DrawEndTime = Now() + UDCTuning::Get().Camera.DrawSeconds;
	DCAudio::PlayAt(this, TEXT("Draw"), GetActorLocation());
	UpdateMoveSpeed();
	OnRep_Slot();
	ServerSetSlot(NewSlot);
}

void ADCCharacter::OnRep_Slot()
{
	RifleMesh->SetVisibility(Slot == EDCSlot::Rifle);
	PhotoCamMesh->SetVisibility(Slot == EDCSlot::Camera);
	BeaconMesh->SetVisibility(Slot == EDCSlot::Stratagem);
	if (Slot != EDCSlot::Stratagem)
	{
		StrikeMarker->SetVisibility(false);
	}
	if (!IsLocallyControlled())
	{
		DrawEndTime = Now() + UDCTuning::Get().Camera.DrawSeconds;
	}
	// The draw: the mannequin plays its equip motion over DrawSeconds while the prop swings in (R1).
	const UDCTuning& T = UDCTuning::Get();
	UAnimSequence* Equip = Cast<UAnimSequence>(T.Assets.DrawAnim.TryLoad());
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (Equip && AnimInstance && HasActorBegunPlay() && !bDowned)
	{
		const float PlayRate = Equip->GetPlayLength() / FMath::Max(T.Camera.DrawSeconds, 0.05f);
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Equip, T.Assets.DrawSlot, 0.05f, 0.1f, PlayRate);
	}
}

void ADCCharacter::ServerSetSlot_Implementation(EDCSlot NewSlot)
{
	Slot = NewSlot;
	bCameraRaised = false;
	DrawEndTime = Now() + UDCTuning::Get().Camera.DrawSeconds * 0.8f;
	UpdateMoveSpeed();
	OnRep_Slot();
}

void ADCCharacter::ServerSetCameraRaised_Implementation(bool bRaised)
{
	bCameraRaised = bRaised && Slot == EDCSlot::Camera && !bDowned;
	UpdateMoveSpeed();
}

void ADCCharacter::ServerSetSprint_Implementation(bool bSprint)
{
	bSprinting = bSprint;
	UpdateMoveSpeed();
}

void ADCCharacter::UpdateMoveSpeed()
{
	const FDCMoveTuning& M = UDCTuning::Get().Move;
	float Speed = bSprinting && !bCameraRaised ? M.SprintSpeed : M.WalkSpeed;
	Speed = bCameraRaised ? M.CameraAimSpeed : Speed;
	Speed = bDowned ? 0.f : Speed; // down on the ground in the fall pose: no sliding
	GetCharacterMovement()->MaxWalkSpeed = Speed;
}

// ---------------------------------------------------------------- rifle

void ADCCharacter::PrimaryRifle()
{
	if (Ammo <= 0)
	{
		DCAudio::PlayAt(this, TEXT("DryFire"), GetActorLocation());
		DoReload();
	}
}

void ADCCharacter::FireRifleOnce()
{
	const FDCRifleTuning& R = UDCTuning::Get().Rifle;
	--Ammo;
	NextFireTime = Now() + 1.f / FMath::Max(R.ShotsPerSecond, 0.1f);

	FVector ViewLoc;
	FRotator ViewRot;
	GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	const float Spread = FMath::DegreesToRadians(R.SpreadDegrees * (GetVelocity().Size2D() > 50.f ? 1.6f : 1.f));
	const FVector Dir = FMath::VRandCone(ViewRot.Vector(), Spread);
	ServerFire(ViewLoc, Dir);

	// Recoil the player can pull against: pitch kick plus a little yaw jitter, recovered on release.
	const float Kick = R.RecoilPitch * FMath::FRandRange(0.8f, 1.2f);
	AddControllerPitchInput(Kick);
	AddControllerYawInput(FMath::FRandRange(-R.RecoilYawJitter, R.RecoilYawJitter));
	RecoilPitch += Kick;
	AddTrauma(0.06f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCRifleFx), false, this);
	const FVector End = ViewLoc + Dir * R.RangeCm;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params);
	PlayRifleFx(RifleMesh->GetComponentLocation(), bHit ? Hit.ImpactPoint : End);

	if (Ammo > 0 && Ammo <= R.LowAmmoWarning)
	{
		DCAudio::Play2D(this, TEXT("LowAmmo"), 0.5f, 1.f + (R.LowAmmoWarning - Ammo) * 0.03f);
	}
	if (Ammo == 0 && Reserve > 0)
	{
		DoReload();
	}
}

void ADCCharacter::ServerFire_Implementation(FVector_NetQuantize Start, FVector_NetQuantizeNormal Dir)
{
	const FDCRifleTuning& R = UDCTuning::Get().Rifle;
	const float T = Now();
	const float MinGap = 0.8f / FMath::Max(R.ShotsPerSecond, 0.1f);
	if (bDowned || Slot != EDCSlot::Rifle || HostAmmo <= 0 || T < HostNextFireTime || FVector::Dist(Start, GetActorLocation()) > 800.f)
	{
		return;
	}
	HostNextFireTime = T + MinGap;
	--HostAmmo;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCRifle), false, this);
	const FVector End = Start + FVector(Dir).GetSafeNormal() * R.RangeCm;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	if (bHit)
	{
		if (ADCEnemy* Enemy = Cast<ADCEnemy>(Hit.GetActor()))
		{
			UGameplayStatics::ApplyPointDamage(Enemy, R.Damage, Dir, Hit, GetController(), this, UDamageType::StaticClass());
			ClientHitConfirm(!Enemy->IsAlive());
		}
	}
	MulticastRifleFx(RifleMesh->GetComponentLocation(), bHit ? Hit.ImpactPoint : End);
}

void ADCCharacter::MulticastRifleFx_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End)
{
	// The shooter already played it locally when the shot left the barrel.
	if (!IsLocallyControlled())
	{
		PlayRifleFx(Start, End);
	}
}

void ADCCharacter::PlayRifleFx(const FVector& Start, const FVector& End)
{
	const FVector Mid = (Start + End) * 0.5f;
	const float Len = FVector::Dist(Start, End);
	Tracer->SetWorldLocationAndRotation(Mid, (End - Start).Rotation());
	Tracer->SetWorldScale3D(FVector(Len / 100.f, 0.03f, 0.03f));
	Tracer->SetVisibility(true);
	TracerHideTime = GetWorld()->GetTimeSeconds() + 0.04f;
	DCAudio::PlayAt(this, TEXT("Rifle"), Start, 0.9f, FMath::FRandRange(0.94f, 1.06f));
}

void ADCCharacter::ClientHitConfirm_Implementation(bool bKill)
{
	LocalFeedback.HitMarkerTime = GetWorld()->GetRealTimeSeconds();
	LocalFeedback.bLastHitKill = bKill;
	DCAudio::Play2D(this, bKill ? TEXT("Kill") : TEXT("HitMarker"), bKill ? 0.8f : 0.6f);
}

void ADCCharacter::ServerReload_Implementation()
{
	const FDCRifleTuning& R = UDCTuning::Get().Rifle;
	const int32 Take = FMath::Min(R.MagazineSize - HostAmmo, HostReserve);
	HostAmmo += Take;
	HostReserve -= Take;
	HostNextFireTime = Now() + R.ReloadSeconds * 0.9f;
}

// ---------------------------------------------------------------- stratagem

void ADCCharacter::UpdateStrikeAim()
{
	bStrikeAimValid = false;
	if (Slot != EDCSlot::Stratagem || bDowned)
	{
		StrikeMarker->SetVisibility(false);
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCStrikeAim), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewRot.Vector() * MaxStrikeRangeCm, ECC_Visibility, Params))
	{
		bStrikeAimValid = true;
		StrikeAimPoint = Hit.ImpactPoint;
	}
	const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(SelectedStratagem);
	StrikeMarker->SetVisibility(bStrikeAimValid && Def);
	if (bStrikeAimValid && Def)
	{
		const float D = Def->BlastRadius * 2.f / 100.f;
		StrikeMarker->SetWorldLocation(StrikeAimPoint + FVector(0, 0, 8.f));
		StrikeMarker->SetWorldScale3D(FVector(D, D, 0.03f));
	}
}

void ADCCharacter::PrimaryStratagem()
{
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(SelectedStratagem);
	if (!PS || !Def || !bStrikeAimValid)
	{
		return;
	}
	if (PS->Points < Def->Cost)
	{
		Toast(FString::Printf(TEXT("NEED %d PROPAGANDA POINTS - TAKE PHOTOS!"), Def->Cost));
		DCAudio::Play2D(this, TEXT("DryFire"));
		return;
	}
	const float Ready = PS->CooldownReadyTime(Def->Id);
	if (Ready > Now())
	{
		Toast(FString::Printf(TEXT("%s RECHARGING %.0fs"), *Def->DisplayName.ToString().ToUpper(), Ready - Now()));
		DCAudio::Play2D(this, TEXT("DryFire"));
		return;
	}
	ServerCallStrike(Def->Id, StrikeAimPoint);
}

void ADCCharacter::ServerCallStrike_Implementation(FName StratagemId, FVector_NetQuantize Target)
{
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(StratagemId);
	ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
	if (!PS || !Def || !GM || !GM->IsCombatLive() || bDowned || Slot != EDCSlot::Stratagem || !PS->OwnsStratagem(StratagemId))
	{
		return;
	}
	const float T = Now();
	if (PS->CooldownReadyTime(StratagemId) > T || FVector::Dist(Target, GetActorLocation()) > MaxStrikeRangeCm || !PS->TrySpend(Def->Cost))
	{
		return;
	}
	PS->SetCooldown(StratagemId, T + Def->CooldownSeconds);
	GM->CallStrike(*Def, Target, GetController());
}

// ---------------------------------------------------------------- camera

void ADCCharacter::PrimaryCamera()
{
	if (!bCameraRaised)
	{
		Toast(TEXT("HOLD RIGHT MOUSE TO RAISE THE CAMERA"));
		return;
	}
	TakePhoto();
}

void ADCCharacter::TakePhoto()
{
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	ADCPlayerController* PC = Cast<ADCPlayerController>(GetController());
	if (!PS || !PC || Now() < NextShutterTime)
	{
		return;
	}
	if (PS->ShotsLeft <= 0)
	{
		Toast(TEXT("OUT OF FILM"));
		DCAudio::Play2D(this, TEXT("DryFire"));
		return;
	}
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	NextShutterTime = Now() + C.WindOnSeconds;

	UTextureRenderTarget2D* Photo = UKismetRenderingLibrary::CreateRenderTarget2D(this, C.PhotoWidth, C.PhotoHeight, RTF_RGBA8);
	Capture->TextureTarget = Photo;
	Capture->FOVAngle = ZoomFOV;
	// The photo is exposed for the fire, not the flash.
	FPostProcessSettings& PhotoLook = Capture->PostProcessSettings;
	PhotoLook.bOverride_BloomIntensity = true;
	PhotoLook.bOverride_AutoExposureBias = true;
	const ADCGameState* PhotoGS = GetWorld()->GetGameState<ADCGameState>();
	const ADCEnvironment* Env = PhotoGS ? PhotoGS->GetEnvironment() : nullptr;
	PhotoLook.BloomIntensity = Env ? Env->GetPhotoBloom(C.PhotoBloomFraction) : UDCTuning::Get().Look.BaseBloom;
	// Stop down while the blast is still blinding: the frame keeps the fire's colour and the debris edges.
	PhotoLook.AutoExposureBias = UDCTuning::Get().Look.ExposureBias + C.PhotoExposureBias - (Env ? Env->GetFlashAlpha() * C.PhotoFlashStopDown : 0.f);
	if (IsSpotlit())
	{
		// A spotlight in the lens blows the frame out: the photo shows it, not just the score.
		PhotoLook.AutoExposureBias += C.SpotlitPhotoStops;
		PhotoLook.BloomIntensity += C.SpotlitPhotoBloom;
	}
	Capture->PostProcessBlendWeight = 1.f;
	Capture->CaptureScene();

	// Instant preview grade from the same rules; the host's grade replaces it when it arrives.
	const FDCShotView View = GetShotView();
	const float ShotTime = Now();
	int32 BlastId = 0;
	const FDCPhotoInputs Inputs = MeasureShot(View, ShotTime, BlastId);
	const FDCPhotoGrade Preview = DCPhotoRules::GradePhoto(Inputs, UDCTuning::Get().Photo);
	const int32 PhotoId = (PS->GetPlayerId() & 0xffff) * 1000 + (++PhotoCounter);
	PC->AddLocalPhoto(PhotoId, Photo, Preview, View, ShotTime);

	LocalFeedback.ShutterTime = GetWorld()->GetRealTimeSeconds();
	DCAudio::Play2D(this, TEXT("Shutter"));
	FTimerHandle Wind;
	GetWorldTimerManager().SetTimer(Wind, FTimerDelegate::CreateWeakLambda(this, [this] { DCAudio::Play2D(this, TEXT("WindOn"), 0.7f); }), 0.18f, false);
	ServerSubmitPhoto(PhotoId, View.Location, View.Rotation, View.FOV, ShotTime);
}

void ADCCharacter::ServerSubmitPhoto_Implementation(int32 PhotoId, FVector_NetQuantize10 CamLoc, FRotator CamRot, float FOV, float ShotTime)
{
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	const float T = Now();
	if (!PS || bDowned || !InSector() || Slot != EDCSlot::Camera || T < HostNextShutterTime || FVector::Dist(CamLoc, GetActorLocation()) > 400.f)
	{
		return;
	}
	if (!PS->ConsumeShot())
	{
		return;
	}
	HostNextShutterTime = T + C.WindOnSeconds * 0.8f;
	FDCShotView View;
	View.Location = CamLoc;
	View.Rotation = CamRot;
	View.FOV = FMath::Clamp(FOV, C.ViewfinderFOVTight, C.ViewfinderFOVWide + PS->ExtraFOV);
	const float When = FMath::Clamp(ShotTime, T - ShotClockToleranceSeconds, T);

	FDCPhotoRecord Record;
	Record.PhotoId = PhotoId;
	Record.OwnerPlayerId = PS->GetPlayerId();
	Record.ShotTime = When;
	Record.CameraLocation = CamLoc;
	Record.CameraRotation = CamRot;
	Record.FOV = View.FOV;
	const FDCPhotoInputs Inputs = MeasureShot(View, When, Record.BlastId);
	Record.Grade = DCPhotoRules::GradePhoto(Inputs, UDCTuning::Get().Photo);
	ApplyStrobe(View);
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		TArray<FDCPoseSample> Poses;
		CopyPoses(When - UDCTuning::Get().Run.ClipSeconds - 0.2f, When + 0.1f, Poses);
		GM->OnPhotoGraded(this, Record, Inputs, MoveTemp(Poses));
	}
	ClientPhotoGraded(PhotoId, Record.Grade);
	MulticastShutterFx();
}

void ADCCharacter::ClientPhotoGraded_Implementation(int32 PhotoId, FDCPhotoGrade Grade)
{
	if (ADCPlayerController* PC = Cast<ADCPlayerController>(GetController()))
	{
		PC->ConfirmLocalPhoto(PhotoId, Grade);
	}
}

void ADCCharacter::MulticastShutterFx_Implementation()
{
	if (!IsLocallyControlled())
	{
		DCAudio::PlayAt(this, TEXT("Shutter"), GetActorLocation(), 0.8f);
	}
}

// ---------------------------------------------------------------- health

float ADCCharacter::TakeDamage(float, const FDamageEvent&, AController*, AActor*)
{
	return 0.f; // all player damage goes through HostApplyHurt so blasts and bites share one path
}

void ADCCharacter::HostApplyHurt(float Amount, const FVector& From)
{
	if (!HasAuthority() || bDowned || Amount <= 0.f)
	{
		return;
	}
	ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
	if (GM && !GM->IsCombatLive())
	{
		return;
	}
	Health = FMath::Max(0.f, Health - Amount);
	ClientHurt(From);
	if (Health <= 0.f)
	{
		bDowned = true;
		DownedAt = Now();
		bCameraRaised = false;
		ReviveProgress = 0.f;
		UpdateMoveSpeed();
		OnRep_Downed();
		if (GM)
		{
			GM->OnPlayerDowned(this);
		}
	}
}

void ADCCharacter::ClientHurt_Implementation(FVector_NetQuantize From)
{
	LocalFeedback.HurtTime = GetWorld()->GetRealTimeSeconds();
	LocalFeedback.HurtFrom = From;
	AddTrauma(0.25f);
	DCAudio::Play2D(this, TEXT("Hurt"), 0.7f, FMath::FRandRange(0.9f, 1.1f));
}

void ADCCharacter::ClientToast_Implementation(const FString& Text) { Toast(Text); }
void ADCCharacter::ClientCue_Implementation(FName Cue) { DCAudio::Play2D(this, Cue, 0.9f); }

void ADCCharacter::ClientSyncAmmo_Implementation(int32 NewAmmo, int32 NewReserve)
{
	Ammo = NewAmmo;
	Reserve = NewReserve;
	ReloadEndTime = -1.f;
}

FDCPhotoInputs ADCCharacter::MeasureShot(const FDCShotView& View, float ShotTime, int32& OutBlastId) const
{
	FDCPhotoInputs In = DCPhotoProbe::Measure(GetWorld(), View, ShotTime, this, IsSpotlit(), OutBlastId);
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	In.ExtraMultiplier = PS ? PS->PhotoMultiplier : 1.f;
	In.DangerScale = PS ? PS->DangerMultiplier : 1.f;
	// Mutators that make blast photos pay more (Solar Eclipse: blasts blaze against the dark sky).
	for (const FDCUpgradeDef* Mutator : GS ? GS->ActiveMutators() : TArray<const FDCUpgradeDef*>())
	{
		In.ExtraMultiplier *= In.bHasBlast ? Mutator->BlastPhotoMultiplier : 1.f;
	}
	return In;
}

void ADCCharacter::ApplyStrobe(const FDCShotView& View)
{
	// Gear with a stun (Strobe Flash): the shutter stuns every enemy in frame within range.
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	for (const FName& GearId : PS ? PS->Gear : TArray<FName>())
	{
		const FDCUpgradeDef* Gear = UDCTuning::Get().FindUpgrade(GearId);
		if (!Gear || Gear->StunSeconds <= 0.f)
		{
			continue;
		}
		const float Until = Now() + Gear->StunSeconds;
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			if (E->IsAlive() && FVector::Dist(E->GetActorLocation(), View.Location) < Gear->StunRangeCm && DCPhotoProbe::InFrame(View, E->GetActorLocation()))
			{
				E->HostStun(Until);
			}
		}
	}
}

void ADCCharacter::RecordPose()
{
	const float T = Now();
	if (T < NextPoseTime)
	{
		return;
	}
	const FDCRunTuning& Run = UDCTuning::Get().Run;
	NextPoseTime = T + 1.f / FMath::Max(Run.PoseSampleHz, 1.f);
	FDCPoseSample& S = PoseRing.AddDefaulted_GetRef();
	S.Time = T;
	S.Location = GetActorLocation();
	const FRotator Aim = GetControlRotation();
	S.Yaw = Aim.Yaw;
	S.Pitch = FRotator::NormalizeAxis(Aim.Pitch);
	S.Speed = GetVelocity().Size2D();
	S.bCamera = bCameraRaised;
	const int32 Keep = FMath::CeilToInt((Run.ClipSeconds + 1.f) * Run.PoseSampleHz);
	if (PoseRing.Num() > Keep)
	{
		PoseRing.RemoveAt(0, PoseRing.Num() - Keep, EAllowShrinking::No);
	}
}

void ADCCharacter::CopyPoses(float From, float To, TArray<FDCPoseSample>& Out) const
{
	for (const FDCPoseSample& S : PoseRing)
	{
		if (S.Time >= From && S.Time <= To)
		{
			Out.Add(S);
		}
	}
}

void ADCCharacter::HostResupply(int32 Film, int32 AmmoAdd, float Heal)
{
	if (!HasAuthority())
	{
		return;
	}
	HostReserve += AmmoAdd;
	Health = FMath::Min(UDCTuning::Get().Move.MaxHealth, Health + Heal);
	if (ADCPlayerState* PS = GetPlayerState<ADCPlayerState>())
	{
		PS->ShotsLeft += Film;
	}
	ClientSyncAmmo(HostAmmo, HostReserve);
	ClientToast(FString::Printf(TEXT("SUPPLY POD: +%d FILM  +%d AMMO  +%.0f HEALTH"), Film, AmmoAdd, Heal));
	ClientCue(TEXT("Pickup"));
}


void ADCCharacter::OnRep_Downed()
{
	UpdateMoveSpeed();
	const UDCTuning& T = UDCTuning::Get();
	if (bDowned)
	{
		DCAudio::PlayAt(this, TEXT("Down"), GetActorLocation());
		bCameraRaised = false;
		// Down on the ground: the mannequin plays its fall and holds the last frame until revived.
		if (UAnimSequence* Fall = Cast<UAnimSequence>(T.Assets.DownedAnim.TryLoad()))
		{
			GetMesh()->PlayAnimation(Fall, false);
		}
		return;
	}
	DCAudio::PlayAt(this, TEXT("Revive"), GetActorLocation());
	if (UClass* Anim = T.Assets.PlayerAnimClass.TryLoadClass<UAnimInstance>())
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		GetMesh()->SetAnimInstanceClass(Anim);
	}
}

void ADCCharacter::ServerSetReviving_Implementation(bool bHolding) { bHostReviveHeld = bHolding; }

void ADCCharacter::HostRevive()
{
	bDowned = false;
	Health = UDCTuning::Get().Move.MaxHealth * 0.6f;
	ReviveProgress = 0.f;
	OnRep_Downed();
}

void ADCCharacter::HostRefill()
{
	const UDCTuning& T = UDCTuning::Get();
	Health = T.Move.MaxHealth;
	HostReserve = T.Rifle.StartReserve;
	HostAmmo = T.Rifle.MagazineSize;
	SelfRevivesLeft = 1;
	ClientSyncAmmo(HostAmmo, HostReserve);
	if (ADCPlayerState* PS = GetPlayerState<ADCPlayerState>())
	{
		PS->ResetFilm(PS->FilmPerSector);
	}
	ClientToast(TEXT("RESUPPLIED: FILM, AMMO, HEALTH"));
}

void ADCCharacter::UpdateRevive(float DeltaSeconds)
{
	if (!bDowned)
	{
		return;
	}
	float ReviveRate = 0.f;
	for (ADCCharacter* Other : TActorRange<ADCCharacter>(GetWorld()))
	{
		if (Other != this && !Other->bDowned && Other->bHostReviveHeld && FVector::Dist(Other->GetActorLocation(), GetActorLocation()) < ReviveRangeCm)
		{
			ReviveRate = 1.f / TeamReviveSeconds;
		}
	}
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const bool bSolo = GS && GS->PlayerArray.Num() <= 1;
	if (ReviveRate <= 0.f && bSolo && bHostReviveHeld && CanSelfRevive())
	{
		ReviveRate = 1.f / SelfReviveSeconds;
	}
	ReviveProgress = ReviveRate > 0.f ? ReviveProgress + ReviveRate * DeltaSeconds : FMath::Max(0.f, ReviveProgress - DeltaSeconds * 0.5f);
	if (ReviveProgress >= 1.f)
	{
		SelfRevivesLeft -= (ReviveRate == 1.f / SelfReviveSeconds) ? 1 : 0;
		HostRevive();
	}
}

// ---------------------------------------------------------------- tick

void ADCCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float Impact = FMath::Abs(GetCharacterMovement()->Velocity.Z);
	DCAudio::PlayAt(this, TEXT("Land"), GetActorLocation() - FVector(0, 0, 90.f), FMath::Clamp(Impact / 900.f, 0.3f, 1.f));
	if (IsLocallyControlled())
	{
		AddTrauma(FMath::Clamp(Impact / 3000.f, 0.f, 0.3f));
	}
}

void ADCCharacter::UpdateFootsteps(float DeltaSeconds)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move->IsMovingOnGround())
	{
		return;
	}
	const float Speed = GetVelocity().Size2D();
	if (Speed < 40.f)
	{
		StepDistance = 0.f;
		return;
	}
	StepDistance += Speed * DeltaSeconds;
	const float Interval = UDCTuning::Get().Move.FootstepIntervalCm * (bIsCrouched ? 0.8f : 1.f);
	if (StepDistance >= Interval)
	{
		StepDistance = 0.f;
		const float Volume = bIsCrouched ? 0.15f : (bSprinting ? 0.6f : 0.35f);
		DCAudio::PlayAt(this, TEXT("Footstep"), GetActorLocation() - FVector(0, 0, 90.f), Volume, FMath::FRandRange(0.85f, 1.15f));
	}
}

void ADCCharacter::UpdateHeldItems(float DeltaSeconds)
{
	// Draw animation: the new item swings up from the hip and grows into the hand over DrawSeconds.
	const float DrawSeconds = FMath::Max(UDCTuning::Get().Camera.DrawSeconds, 0.01f);
	const float U = 1.f - FMath::Clamp((DrawEndTime - Now()) / DrawSeconds, 0.f, 1.f);
	const float Ease = 1.f - FMath::Square(1.f - U);
	const FRotator Swing(0.f, 0.f, -80.f * (1.f - Ease));
	RifleMesh->SetRelativeScale3D(FVector(0.75f, 0.08f, 0.12f) * FMath::Lerp(0.3f, 1.f, Ease));
	RifleMesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f) + Swing);
	PhotoCamMesh->SetRelativeScale3D(FVector(0.2f, 0.12f, 0.14f) * FMath::Lerp(0.3f, 1.f, Ease));
	PhotoCamMesh->SetRelativeLocation(bCameraRaised ? FVector(-12.f, 20.f, 12.f) : FVector(-6.f, 8.f, 0.f));
	PhotoCamMesh->SetRelativeRotation(Swing);
	BeaconMesh->SetRelativeScale3D(FVector(0.08f, 0.08f, 0.3f) * FMath::Lerp(0.3f, 1.f, Ease));
	BeaconMesh->SetRelativeRotation(Swing);

	if (Tracer->IsVisible() && GetWorld()->GetTimeSeconds() > TracerHideTime)
	{
		Tracer->SetVisibility(false);
	}
}

void ADCCharacter::UpdateView(float DeltaSeconds)
{
	const float Target = bCameraRaised ? 1.f : 0.f;
	ViewBlend = FMath::FInterpConstantTo(ViewBlend, Target, GetWorld()->GetDeltaSeconds(), 8.f);
	const bool bFirst = ViewBlend > 0.5f;
	if (FirstCam->IsActive() != bFirst)
	{
		FirstCam->SetActive(bFirst);
		ThirdCam->SetActive(!bFirst);
		GetMesh()->SetOwnerNoSee(bFirst);
		RifleMesh->SetOwnerNoSee(bFirst);
		PhotoCamMesh->SetOwnerNoSee(bFirst);
		BeaconMesh->SetOwnerNoSee(bFirst);
	}
	FirstCam->SetFieldOfView(FMath::FInterpTo(FirstCam->FieldOfView, ZoomFOV, DeltaSeconds, 14.f));

	// Trauma shake: offsets the camera only, never the aim.
	Trauma = FMath::Max(0.f, Trauma - DeltaSeconds * 1.6f);
	const float Shake = Trauma * Trauma;
	const float T = GetWorld()->GetRealTimeSeconds();
	const FRotator Jitter(FMath::PerlinNoise1D(T * 23.f) * 4.f * Shake, FMath::PerlinNoise1D(T * 19.f + 7.f) * 4.f * Shake, FMath::PerlinNoise1D(T * 17.f + 3.f) * 3.f * Shake);
	ThirdCam->SetRelativeRotation(Jitter);
	FirstCam->SetRelativeRotation(Jitter * 0.6f);
}

void ADCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHeldItems(DeltaSeconds);
	UpdateFootsteps(DeltaSeconds);
	if (HasAuthority())
	{
		UpdateRevive(DeltaSeconds);
		RecordPose();
	}
	if (!IsLocallyControlled() || !GetController())
	{
		return;
	}
	UpdateView(DeltaSeconds);
	UpdateStrikeAim();

	const FDCRifleTuning& R = UDCTuning::Get().Rifle;
	if (IsReloading() && Now() >= ReloadEndTime)
	{
		const int32 Take = FMath::Min(R.MagazineSize - Ammo, Reserve);
		Ammo += Take;
		Reserve -= Take;
		ReloadEndTime = -1.f;
	}
	const bool bCanShoot = Slot == EDCSlot::Rifle && !bDowned && !IsDrawing() && !IsReloading() && Ammo > 0;
	if (bTriggerHeld && bCanShoot && Now() >= NextFireTime)
	{
		FireRifleOnce();
	}
	if (!bTriggerHeld && RecoilPitch > 0.f)
	{
		const float Back = RecoilPitch * FMath::Min(1.f, R.RecoilRecoverPerSecond * DeltaSeconds);
		AddControllerPitchInput(-Back);
		RecoilPitch -= Back;
	}
	if (bCameraRaised && !bAimHeld)
	{
		bCameraRaised = false;
		ServerSetCameraRaised(false);
	}
}
