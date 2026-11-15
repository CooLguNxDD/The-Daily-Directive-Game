#include "DCCharacter.h"
#include "DCFrameScopes.h"
#include "DCAnimInstance.h"

#include "Components/PointLightComponent.h"
#include "DCOutfitBuild.h"

#include "DCVoxelCity.h"

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
#include "DCPresentationClock.h"
#include "DCReelDirector.h"
#include "DCTuning.h"
#include "DCUpgrades.h"
#include "Interfaces/MovementBaseInterface.h"
#include "Voxel/DCVoxelSettings.h"
#include "Voxel/DCVoxelModel.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/WorldSettings.h"
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
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR; // set per shot from Print.bSceneLinear

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
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(RootComponent);
	RimLight->SetCastShadows(false);
	RimLight->SetVisibility(false);

	StrikeMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StrikeMarker"));
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
	MuzzleFlash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MuzzleFlash"));
	MuzzleFlash->SetupAttachment(RootComponent);
	MuzzleFlash->SetUsingAbsoluteLocation(true);
	MuzzleFlash->SetUsingAbsoluteScale(true);
	MuzzleFlash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MuzzleFlash->SetCastShadow(false);
	MuzzleFlash->SetVisibility(false);
}

void ADCCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCCharacter, Slot);
	DOREPLIFETIME(ADCCharacter, WeaponIndex);
	DOREPLIFETIME(ADCCharacter, bCameraRaised);
	DOREPLIFETIME(ADCCharacter, bRifleAim);
	DOREPLIFETIME_CONDITION(ADCCharacter, AimPitch, COND_SkipOwner);
	DOREPLIFETIME(ADCCharacter, bSprinting);
	DOREPLIFETIME(ADCCharacter, Health);
	DOREPLIFETIME(ADCCharacter, bDowned);
	DOREPLIFETIME(ADCCharacter, bLastFrameReady);
	DOREPLIFETIME(ADCCharacter, SpotlitUntil);
	DOREPLIFETIME(ADCCharacter, ReviveProgress);
	DOREPLIFETIME(ADCCharacter, SelfRevivesLeft);
	DOREPLIFETIME(ADCCharacter, DownedAt);
}

void ADCCharacter::BeginPlay()
{
	Super::BeginPlay();
	// The photographer is never in their own photo: a downed Last Frame lens sits inside the body and outfit (the
	// packaged r14-solo2 run printed a solid black Last Frame on the front page). Teammates stay in frame (co-star).
	Capture->HiddenActors.AddUnique(this);
	const UDCTuning& T = UDCTuning::Get();

	if (USkeletalMesh* Mannequin = Cast<USkeletalMesh>(T.Assets.PlayerMesh.TryLoad()))
	{
		GetMesh()->SetSkeletalMesh(Mannequin);
		if (UClass* Anim = T.Assets.PlayerAnimClass.TryLoadClass<UAnimInstance>())
		{
			GetMesh()->SetAnimInstanceClass(Anim);
		}
		// R16: the aim pose lives in UDCAnimInstance's proxy; a plain AnimBP shows no aim pitch (logged once per pawn).
		UE_LOG(LogTemp, Display, TEXT("DCAIM pose %s: anim class %s"), Cast<UDCAnimInstance>(GetMesh()->GetAnimInstance()) ? TEXT("on") : TEXT("OFF"), *T.Assets.PlayerAnimClass.ToString());
	}
	// R10: the rifle locomotion clips load once here, never in Tick.
	for (const FSoftObjectPath& P : T.Assets.RifleJogClips)
	{
		RifleJogLoaded.Add(Cast<UAnimSequenceBase>(P.TryLoad()));
	}
	for (const FSoftObjectPath& P : T.Assets.RifleWalkClips)
	{
		RifleWalkLoaded.Add(Cast<UAnimSequenceBase>(P.TryLoad()));
	}
	RifleIdleLoaded = Cast<UAnimSequenceBase>(T.Assets.RifleIdleClip.TryLoad());

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
	// The viewfinder tunnel is the post stack's Viewfinder layer (R19), weighted while the camera is raised.
	FirstCam->PostProcessBlendWeight = 0.f;

	const FDCLookTuning& Look = T.Look;
	const FName Hand = T.Assets.HandSocket;
	RifleMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	RifleMesh->SetStaticMesh(DCAssets::Cube());
	RifleMesh->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.08f, 0.08f, 0.07f)));
	RifleMesh->SetRelativeLocation(T.Assets.RifleGripLocation);
	RifleMesh->SetRelativeRotation(T.Assets.RifleGripRotation);
	// R8: the forge model of the weapon in hand replaces the cube (InitWeapons, DCWeapons.cpp); it rides on the rifle
	// prop, so hand and holster placement still apply.
	PhotoCamMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	PhotoCamMesh->SetStaticMesh(DCAssets::Cube());
	PhotoCamMesh->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.024f, 0.552f, 0.687f), 0.6f));
	if (const FDCVoxelMeshData* Cam = DCVoxelModels::Mesh(T.Assets.ChestCamModel))
	{
		ChestCam = NewObject<UProceduralMeshComponent>(this, TEXT("ChestCam"));
		ChestCam->SetupAttachment(GetMesh(), T.Assets.ChestCamSocket);
		ChestCam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ChestCam->RegisterComponent();
		ChestCam->CreateMeshSection(0, Cam->Positions, Cam->Indices, Cam->Normals, Cam->UVs, Cam->Colors, {}, false);
		ChestCam->SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
		ChestCam->SetRelativeLocationAndRotation(T.Assets.ChestCamLocation, T.Assets.ChestCamRotation);
		ChestCam->SetRelativeScale3D(FVector(T.Assets.ChestCamScale));
		ChestCam->SetVisibility(Slot != EDCSlot::Camera);
	}
	BeaconMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
	BeaconMesh->SetStaticMesh(DCAssets::Cylinder());
	BeaconMesh->SetMaterial(0, DCAssets::MakeFlat(this, Look.Accent, 12.f));
	// Dark suit (one flat material over every body slot) and the cyan rim that keeps it readable at night.
	UMaterialInstanceDynamic* Suit = DCAssets::MakeFlat(this, Look.SuitColor);
	for (int32 m = 0; m < GetMesh()->GetNumMaterials(); ++m)
	{
		GetMesh()->SetMaterial(m, Suit);
	}
	RimLight->SetRelativeLocation(Look.PlayerRimOffset);
	RimLight->SetIntensityUnits(ELightUnits::Candelas);
	RimLight->SetIntensity(Look.PlayerRimCandela);
	RimLight->SetLightColor(Look.OutfitRimColor);
	RimLight->SetAttenuationRadius(Look.PlayerRimReachCm);
	RimLight->SetVisibility(Look.PlayerRimCandela > 0.f);

	StrikeMarker->SetMaterial(0, DCAssets::MakeFlatUnlit(this, Look.Accent, Look.DangerRingGlow)); // unlit: stays palette red, not lit to salmon
	Tracer->SetStaticMesh(DCAssets::Cube());
	Tracer->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(1.f, 0.8f, 0.4f), 60.f));
	MuzzleFlash->SetStaticMesh(DCAssets::Sphere());
	MuzzleFlash->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(1.f, 0.7f, 0.3f), 120.f));

	InitWeapons();
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

float ADCCharacter::LensFOV() const
{
	// Fisheye-type gear widens the whole zoom range by its extra degrees (R10: the view itself changes).
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	return ZoomFOV + (PS ? PS->ExtraFOV : 0.f);
}

FDCShotView ADCCharacter::GetShotView() const
{
	FDCShotView View;
	View.Location = FirstCam->GetComponentLocation();
	View.Rotation = GetControlRotation();
	View.FOV = LensFOV();
	if (HasLastFrame())
	{
		// The Last Frame lens (R21): low, along the player's aim; the capture uses the same view, so the grade matches.
		View.Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Rotation.Pitch), -4.f, 35.f);
		View.Location = GetActorLocation() + FVector(0, 0, 35.f) + View.Rotation.Vector() * 60.f;
	}
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
	if (ADCReelDirector* Director = ReelDirector())
	{
		Director->DollyTake(V); // R16: second-take camera dolly
		return;
	}
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
	if (HasLastFrame() && InSector())
	{
		TakePhoto(); // R21: one last shutter from the ground, whatever was in hand
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
	if (Slot == EDCSlot::Rifle && !bDowned && InSector())
	{
		SetRifleAim(true); // R16
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
	SetRifleAim(false);
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
	const FDCRifleTuning& R = ActiveWeapon().Stats;
	if (Slot != EDCSlot::Rifle || IsReloading() || Reserve <= 0 || Ammo >= R.MagazineSize || bDowned)
	{
		return;
	}
	ReloadEndTime = Now() + R.ReloadSeconds;
	DCAudio::PlayAt(this, TEXT("Reload"), GetActorLocation());
	ServerReload();
}

void ADCCharacter::OnSlot1(const FInputActionInstance&) { SelectSlot(EDCSlot::Rifle); } // R8: Q or the wheel switch weapons on it
void ADCCharacter::OnSlot2(const FInputActionInstance&) { SelectSlot(EDCSlot::Stratagem); }
void ADCCharacter::OnSlot3(const FInputActionInstance&) { SelectSlot(EDCSlot::Camera); }

void ADCCharacter::OnZoom(const FInputActionInstance& I)
{
	const float Wheel = I.GetValue().Get<float>();
	if (ADCReelDirector* Director = ReelDirector())
	{
		Director->ZoomTake(Wheel);
		return;
	}
	if (Slot != EDCSlot::Camera && !bCameraRaised)
	{
		OnCycle(I); // the wheel cycles the slot's list (weapons, stratagems); it zooms only the camera
		return;
	}
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const float Before = ZoomFOV;
	ZoomFOV = FMath::Clamp(ZoomFOV - Wheel * C.ZoomStep, C.ViewfinderFOVTight, C.ViewfinderFOVWide);
	if (!FMath::IsNearlyEqual(Before, ZoomFOV))
	{
		DCAudio::Play2D(this, TEXT("UIClick"), 0.25f, ZoomFOV < Before ? 1.3f : 0.9f); // lens ring tick
	}
}

void ADCCharacter::OnCycle(const FInputActionInstance&)
{
	// What cycles is per slot (R8): the gun slot switches weapons, every other slot the stratagem.
	using FCycleFn = void (ADCCharacter::*)();
	static const TMap<EDCSlot, FCycleFn> Cycles = {{EDCSlot::Rifle, &ADCCharacter::NextWeapon}};
	const FCycleFn* Fn = Cycles.Find(Slot);
	(this->*(Fn ? *Fn : &ADCCharacter::NextStratagem))();
}

void ADCCharacter::NextStratagem()
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
	SetRifleAim(false);
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
	// The rifle is never hidden: in the hand for slot 1, slung on the back otherwise (R1 holster).
	const FDCAssetTuning& A = UDCTuning::Get().Assets;
	RifleMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Slot == EDCSlot::Rifle ? A.HandSocket : A.HolsterSocket);
	RifleMesh->SetRelativeLocation(Slot == EDCSlot::Rifle ? A.RifleGripLocation : A.HolsterLocation);
	RifleMesh->SetVisibility(true);
	PhotoCamMesh->SetVisibility(Slot == EDCSlot::Camera);
	if (ChestCam)
	{
		ChestCam->SetVisibility(Slot != EDCSlot::Camera); // the camera is either in hand or on the chest, never gone
	}
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
	const FSoftObjectPath* SlotAnim = T.Assets.SlotDrawAnims.Find(Slot);
	UAnimSequence* Equip = Cast<UAnimSequence>((SlotAnim ? *SlotAnim : T.Assets.DrawAnim).TryLoad());
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	// Once per change: the server's echo of a slot the owner already drew does not restart the motion.
	if (Equip && AnimInstance && HasActorBegunPlay() && !bDowned && DrawnSlot != Slot)
	{
		DrawnSlot = Slot;
		const float PlayRate = Equip->GetPlayLength() / FMath::Max(T.Camera.DrawSeconds, 0.05f);
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Equip, T.Assets.DrawSlot, 0.05f, 0.1f, PlayRate);
		LocoBlockedUntil = GetWorld()->GetTimeSeconds() + T.Camera.DrawSeconds; // the rifle loop resumes after the draw
		LocoClip = nullptr;
	}
}

void ADCCharacter::UpdateRifleLoco()
{
	// R10: rifle drawn -> the rifle locomotion clip for this pawn's movement loops on DrawSlot (legs run, rifle up).
	const FDCAssetTuning& A = UDCTuning::Get().Assets;
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}
	// Human 15:50 (ship blocker): the rifle walk/jog clips carry root motion, and a montage's root motion drove the capsule
	// (the pawn walked on by itself, WASD and sprint lost). Movement is the character movement's job: never root motion.
	if (AnimInstance->RootMotionMode != ERootMotionMode::IgnoreRootMotion)
	{
		AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	}
	UAnimSequenceBase* Want = nullptr;
	const bool bRifleUp = Slot == EDCSlot::Rifle && !bDowned && GetWorld()->GetTimeSeconds() >= LocoBlockedUntil;
	const FVector Flat(GetVelocity().X, GetVelocity().Y, 0.f);
	const float Speed = Flat.Size();
	if (bRifleUp && Speed < A.RifleIdleSpeedCm)
	{
		Want = RifleIdleLoaded;
	}
	else if (bRifleUp)
	{
		// Direction bucket, clockwise from forward in 45 degree steps (the clip tables' order).
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Flat, GetActorRightVector()), FVector::DotProduct(Flat, GetActorForwardVector())));
		const int32 Bucket = (FMath::RoundToInt32(Deg / 45.f) + 8) % 8;
		const TArray<TObjectPtr<UAnimSequenceBase>>& Gait = Speed >= A.RifleJogSpeedCm ? RifleJogLoaded : RifleWalkLoaded;
		Want = Gait.IsValidIndex(Bucket) ? Gait[Bucket].Get() : nullptr;
	}
	if (Want == LocoClip)
	{
		return;
	}
	if (LocoClip && !Want)
	{
		AnimInstance->StopSlotAnimation(A.RifleLocoBlendSeconds, A.DrawSlot);
	}
	LocoClip = Want;
	if (Want)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Want, A.DrawSlot, A.RifleLocoBlendSeconds, A.RifleLocoBlendSeconds, 1.f, 100000);
	}
}

void ADCCharacter::ApplyPhotoLook(const UWorld* World, FPostProcessSettings& PhotoLook, bool bSpotlit)
{
	// The photo's look is the post stack composed for a photo (R5, R19): the scene grade, a fraction of the bloom
	// spike, a stop-down while the flash is on screen, and the spotlight blow-out. One look for live shots and takes.
	const ADCGameState* PhotoGS = World ? World->GetGameState<ADCGameState>() : nullptr;
	const ADCEnvironment* Env = PhotoGS ? PhotoGS->GetEnvironment() : nullptr;
	if (Env)
	{
		ADCEnvironment::ApplyPost(Env->ComposeLook(true, bSpotlit), PhotoLook);
	}
}

void ADCCharacter::ServerSetSlot_Implementation(EDCSlot NewSlot)
{
	if (IsLocallyControlled() && Slot == NewSlot)
	{
		return; // listen-server host: SelectSlot already applied it locally; don't restart the draw
	}
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

void ADCCharacter::SetRifleAim(bool bAim)
{
	// R16: the owner predicts, the host validates (rifle out, standing) and replicates to the others.
	if (bRifleAim == bAim)
	{
		return;
	}
	bRifleAim = bAim;
	UpdateMoveSpeed();
	ServerSetRifleAim(bAim);
	DCAudio::Play2D(this, TEXT("Draw"), bAim ? 0.35f : 0.2f, bAim ? 1.05f : 0.95f);
}

void ADCCharacter::ServerSetRifleAim_Implementation(bool bAim)
{
	bRifleAim = bAim && Slot == EDCSlot::Rifle && !bDowned;
	UpdateMoveSpeed();
}

void ADCCharacter::ServerAimPitch_Implementation(float Pitch)
{
	AimPitch = FMath::Clamp(Pitch, -89.f, 89.f); // the others' aim pose only; never used for hits
}

void ADCCharacter::ServerSetSprint_Implementation(bool bSprint)
{
	bSprinting = bSprint;
	UpdateMoveSpeed();
}

void ADCCharacter::UpdateMoveSpeed()
{
	const FDCMoveTuning& M = UDCTuning::Get().Move;
	float Speed = bSprinting && !bCameraRaised && !bRifleAim ? M.SprintSpeed : M.WalkSpeed;
	Speed = bCameraRaised ? M.CameraAimSpeed : Speed;
	Speed = bRifleAim ? UDCTuning::Get().Aim.MoveSpeed : Speed; // R16
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
	const FDCWeaponDef& W = ActiveWeapon();
	const FDCRifleTuning& R = W.Stats;
	--Ammo;
	NextFireTime = Now() + 1.f / FMath::Max(R.ShotsPerSecond, 0.1f);

	FVector ViewLoc;
	FRotator ViewRot;
	GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	const float SpreadScale = bRifleAim ? UDCTuning::Get().Aim.SpreadScale : (GetVelocity().Size2D() > 50.f ? W.MoveSpreadScale : 1.f); // R16
	const float Spread = FMath::DegreesToRadians(R.SpreadDegrees * SpreadScale);
	const FVector Dir = FMath::VRandCone(ViewRot.Vector(), Spread);
	ServerFire(ViewLoc, Dir);

	// Recoil the player can pull against: pitch kick plus a little yaw jitter, recovered on release.
	const float Kick = R.RecoilPitch * FMath::FRandRange(0.8f, 1.2f);
	AddControllerPitchInput(Kick);
	AddControllerYawInput(FMath::FRandRange(-R.RecoilYawJitter, R.RecoilYawJitter));
	RecoilPitch += Kick;
	AddTrauma(W.Trauma);

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
	const FDCWeaponDef& W = UDCTuning::Get().Weapon(HostWeapon);
	const FDCRifleTuning& R = W.Stats;
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
	// Demo telemetry (-dcrifletrace): what the rifle hits, every 2 s (the sector-2 downs: is the pilot hitting?).
	static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("dcrifletrace"));
	if (bTrace)
	{
		static int32 Shots = 0, EnemyHits = 0;
		static FString LastOther;
		static double NextLog = 0.0;
		++Shots;
		EnemyHits += bHit && Cast<ADCEnemy>(Hit.GetActor()) ? 1 : 0;
		LastOther = bHit && !Cast<ADCEnemy>(Hit.GetActor()) ? FString::Printf(TEXT("%s@%.0fcm"), Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("?"), Hit.Distance) : LastOther;
		if (GetWorld()->GetRealTimeSeconds() >= NextLog)
		{
			NextLog = GetWorld()->GetRealTimeSeconds() + 2.0;
			UE_LOG(LogTemp, Display, TEXT("DCRIFLE shots=%d enemyhits=%d lastother=%s health=%.0f ammo=%d"), Shots, EnemyHits, *LastOther, Health, HostAmmo);
		}
	}
	// R8: a scattergun's extra pellets spread around the aimed ray on the host (the host owns the hits).
	bool bAnyHit = false;
	bool bAnyKill = false;
	const float PelletCone = FMath::DegreesToRadians(R.SpreadDegrees);
	ADCVoxelCity* ChipCity = ADCVoxelCity::IsEnabled() ? ADCVoxelCity::Get(this) : nullptr; // R24: hits wear the city
	for (int32 p = 0; p < FMath::Clamp(W.Pellets, 1, 24); ++p)
	{
		FHitResult PelletHit = Hit;
		const FVector PelletDir = p == 0 ? FVector(Dir) : FMath::VRandCone(FVector(Dir), PelletCone);
		const bool bPelletHit = p == 0 ? bHit : GetWorld()->LineTraceSingleByChannel(PelletHit, Start, Start + PelletDir * R.RangeCm, ECC_Visibility, Params);
		ADCEnemy* Enemy = bPelletHit ? Cast<ADCEnemy>(PelletHit.GetActor()) : nullptr;
		if (Enemy && Enemy->IsAlive())
		{
			UGameplayStatics::ApplyPointDamage(Enemy, R.Damage, PelletDir, PelletHit, GetController(), this, UDamageType::StaticClass());
			bAnyHit = true;
			bAnyKill |= !Enemy->IsAlive();
		}
		else if (bPelletHit && ChipCity)
		{
			ChipCity->HostChip(PelletHit, W, this);
		}
	}
	if (bAnyHit)
	{
		ClientHitConfirm(bAnyKill);
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

void ADCCharacter::PlayRifleFx(const FVector& HostStart, const FVector& End)
{
	// R10: every machine starts the tracer at its own muzzle (rifle mesh + data offset): the host's idea of a remote
	// pawn's pose can differ. A brief flash marks the barrel.
	const FDCRifleTuning& RT = UDCTuning::Get().Rifle;
	const FVector Start = RifleMesh ? RifleMesh->GetComponentTransform().TransformPosition(RT.MuzzleOffsetCm) : HostStart;
	MuzzleFlash->SetWorldLocation(Start);
	MuzzleFlash->SetWorldScale3D(FVector(RT.MuzzleFlashScale * FMath::FRandRange(0.8f, 1.2f)));
	MuzzleFlash->SetVisibility(true);
	MuzzleHideTime = GetWorld()->GetTimeSeconds() + RT.MuzzleFlashSeconds;
	const FVector Mid = (Start + End) * 0.5f;
	const float Len = FVector::Dist(Start, End);
	Tracer->SetWorldLocationAndRotation(Mid, (End - Start).Rotation());
	Tracer->SetWorldScale3D(FVector(Len / 100.f, 0.03f, 0.03f));
	Tracer->SetVisibility(true);
	const FDCWeaponDef& W = ActiveWeapon();
	Tracer->SetWorldScale3D(FVector(Len / 100.f, W.TracerWidth, W.TracerWidth));
	TracerHideTime = GetWorld()->GetTimeSeconds() + W.TracerSeconds;
	DCAudio::PlayAt(this, W.Sound, Start, W.SoundVolume, FMath::FRandRange(0.94f, 1.06f));
}

void ADCCharacter::ClientHitConfirm_Implementation(bool bKill)
{
	LocalFeedback.HitMarkerTime = GetWorld()->GetRealTimeSeconds();
	LocalFeedback.bLastHitKill = bKill;
	DCAudio::Play2D(this, bKill ? TEXT("Kill") : TEXT("HitMarker"), bKill ? 0.8f : 0.6f);
}

void ADCCharacter::ServerReload_Implementation()
{
	const FDCRifleTuning& R = UDCTuning::Get().Weapon(HostWeapon).Stats;
	const int32 Take = FMath::Min(R.MagazineSize - HostAmmo, HostReserve);
	HostAmmo += Take;
	HostReserve -= Take;
	HostNextFireTime = Now() + R.ReloadSeconds * 0.9f;
	// The owner's count follows the host's once the reload would have finished (a slot change can cancel the
	// owner's local reload; the host has already moved the rounds).
	FTimerHandle Sync;
	GetWorldTimerManager().SetTimer(Sync, FTimerDelegate::CreateWeakLambda(this, [this, Reloaded = HostWeapon] {
		// The reloaded weapon's counts, even when the owner switched away since (R8).
		const bool bInHand = Reloaded == HostWeapon || !HostWeaponStash.IsValidIndex(Reloaded);
		const FIntPoint Counts = bInHand ? FIntPoint(HostAmmo, HostReserve) : HostWeaponStash[Reloaded];
		ClientSyncAmmo(Counts.X, Counts.Y, bInHand ? HostWeapon : Reloaded);
	}), R.ReloadSeconds, false);
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
		// A thin ring at the blast radius (a filled disc hid the street it marks), rebuilt only when the radius changes.
		if (Def->BlastRadius != StrikeMarkerRadius)
		{
			StrikeMarkerRadius = Def->BlastRadius;
			DCAssets::BuildRing(StrikeMarker, StrikeMarkerRadius);
		}
		StrikeMarker->SetWorldLocation(StrikeAimPoint + FVector(0, 0, 8.f));
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
	DC_FRAME_SCOPE("TakePhoto");
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	ADCPlayerController* PC = Cast<ADCPlayerController>(GetController());
	if (!PS || !PC || Now() < NextShutterTime)
	{
		return;
	}
	if (PS->ShotsLeft <= 0 && !HasLastFrame()) // the Last Frame needs no film (R21)
	{
		Toast(TEXT("OUT OF FILM"));
		DCAudio::Play2D(this, TEXT("DryFire"));
		return;
	}
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	NextShutterTime = Now() + C.WindOnSeconds;

	UTextureRenderTarget2D* Photo = UKismetRenderingLibrary::CreateRenderTarget2D(this, C.PhotoWidth, C.PhotoHeight, RTF_RGBA16f); // float: the print meter has range (advisor CP21)
	Capture->TextureTarget = Photo;
	Capture->CaptureSource = C.Print.bSceneLinear ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalColorLDR; // advisor CP28
	Capture->FOVAngle = LensFOV();
	ApplyPhotoLook(GetWorld(), Capture->PostProcessSettings, IsSpotlit());
	Capture->PostProcessBlendWeight = 1.f;
	// Never shoot from inside a voxel (a tower's hollow, rubble, a prop): back the lens out along the view, then up.
	const FVector Lens = Capture->GetComponentLocation();
	const FRotator LensRot = Capture->GetComponentRotation();
	if (HasLastFrame())
	{
		// R21: from the ground, looking where the player looks, never into the dirt.
		FRotator Aim = GetControlRotation();
		Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -4.f, 35.f);
		Capture->SetWorldLocationAndRotation(GetActorLocation() + FVector(0, 0, 35.f) + Aim.Vector() * 60.f, Aim);
		// The swarmer biting a downed player sits on the lens: anything that close is hidden for this one shot (CP6:
		// a Last Frame printed solid black from inside an ink swarmer).
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			if (FVector::Dist(E->GetActorLocation(), Capture->GetComponentLocation()) < UDCTuning::Get().Photo.LastFrameClearCm)
			{
				Capture->HiddenActors.AddUnique(E);
				LensHidden.Add(E);
			}
		}
	}
	if (const ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		const FVector From = Capture->GetComponentLocation(); // the Last Frame lens when downed, else the camera
		FVector Free = From;
		const FVector Back = -Capture->GetForwardVector();
		for (int32 Step = 1; Step <= 24 && City->IsSolidAt(Free); ++Step)
		{
			Free = From + Back * (Step * 25.f) + FVector(0, 0, Step * 10.f);
		}
		Capture->SetWorldLocation(Free);
	}
	Capture->CaptureScene();
	Capture->SetWorldLocationAndRotation(Lens, LensRot);
	for (const TWeakObjectPtr<AActor>& Hidden : LensHidden)
	{
		Capture->HiddenActors.Remove(Hidden.Get());
	}
	LensHidden.Reset();

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
	const bool bLast = HasLastFrame();
	if (!PS || (bDowned && !bLast) || !InSector() || (Slot != EDCSlot::Camera && !bLast) || T < HostNextShutterTime || FVector::Dist(CamLoc, GetActorLocation()) > 400.f)
	{
		return;
	}
	bLastFrameReady = bLastFrameReady && !bLast; // spent
	if (!bLast && !PS->ConsumeShot())
	{
		return;
	}
	HostNextShutterTime = T + C.WindOnSeconds * 0.8f;
	HostGradeShot(PhotoId, CamLoc, CamRot, FMath::Clamp(FOV, C.ViewfinderFOVTight, C.ViewfinderFOVWide + PS->ExtraFOV),
		FMath::Clamp(ShotTime, T - ShotClockToleranceSeconds, T), bLast, GetWorld()->GetRealTimeSeconds() + C.GradeWaitForOpSeconds);
}

void ADCCharacter::TickOutfit()
{
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	// An empty code is the default outfit (the voxel body must replace the mannequin either way).
	if (!PS || (bOutfitApplied && PS->OutfitCode == AppliedOutfit))
	{
		return;
	}
	bOutfitApplied = true;
	AppliedOutfit = PS->OutfitCode;
	// The outfit's camera replaces the default chest camera (same rule: shown while the camera is not in hand).
	UProceduralMeshComponent* Cam = DCOutfitBuild::Apply(GetMesh(), DCOutfitBuild::FromCode(AppliedOutfit), OutfitMeshes);
	// The default chest camera goes once an outfit camera exists; an earlier outfit camera was already destroyed by Apply.
	if (Cam && IsValid(ChestCam) && !ChestCam->IsBeingDestroyed() && ChestCam != Cam)
	{
		ChestCam->DestroyComponent();
	}
	if (Cam)
	{
		ChestCam = Cam;
	}
	HideBodyFromOwner(FirstCam->IsActive());
	if (ChestCam)
	{
		ChestCam->SetVisibility(Slot != EDCSlot::Camera);
	}
}

void ADCCharacter::SetBase(FMovementBaseInterfaceData* MovementBaseInterfaceData, const FName BoneName, bool bNotifyActor)
{
	const UActorComponent* Base = MovementBaseInterfaceData ? Cast<UActorComponent>(MovementBaseInterfaceData->GetMovementBaseObject()) : nullptr;
	// No movement base a remote machine cannot resolve: voxel meshes (rebuilt per machine) and any actor spawned at run
	// time without replication (the environment's Ground slab: co-op clients flooded unresolved-GUID warnings, CP6).
	const AActor* BaseOwner = Base ? Base->GetOwner() : nullptr;
	if (BaseOwner && (Cast<ADCVoxelCity>(BaseOwner) || (!BaseOwner->GetIsReplicated() && !BaseOwner->IsNetStartupActor())))
	{
		Super::SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr), NAME_None, bNotifyActor);
		return;
	}
	Super::SetBase(MovementBaseInterfaceData, BoneName, bNotifyActor);
}

void ADCCharacter::HostGradeShot(int32 PhotoId, FVector CamLoc, FRotator CamRot, float FOV, float When, bool bLast, double GiveUpAt)
{
	// A strike's collapse records are written when its voxel op leaves the worker, a few frames after the blast. A
	// shutter in that gap waits for them (graded at its own shot time; the records carry the blast time), so PEAK
	// CINEMA and DEMOLITION see the towers the strike felled.
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	if (City && City->HasPendingOpsAt(When) && GetWorld()->GetRealTimeSeconds() < GiveUpAt)
	{
		UE_LOG(LogTemp, Display, TEXT("DCPHOTO wait-op photo=%d shot=%.2f"), PhotoId, When);
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [=, this]
		{
			HostGradeShot(PhotoId, CamLoc, CamRot, FOV, When, bLast, GiveUpAt);
		}));
		return;
	}
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	if (!PS)
	{
		return;
	}
	FDCShotView View;
	View.Location = CamLoc;
	View.Rotation = CamRot;
	View.FOV = FOV;

	FDCPhotoRecord Record;
	Record.PhotoId = PhotoId;
	Record.OwnerPlayerId = PS->GetPlayerId();
	Record.ShotTime = When;
	Record.CameraLocation = CamLoc;
	Record.CameraRotation = CamRot;
	Record.FOV = View.FOV;
	FDCPhotoInputs Inputs = MeasureShot(View, When, Record.BlastId);
	if (bLast)
	{
		// Capped at the best living shot of this sector, every photographer's (R21).
		Inputs.bLastFrame = true;
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		for (const APlayerState* Other : GS ? GS->PlayerArray : TArray<TObjectPtr<APlayerState>>())
		{
			if (const ADCPlayerState* OtherPS = Cast<ADCPlayerState>(Other))
			{
				for (const FDCPhotoRecord& P : OtherPS->Photos)
				{
					Inputs.LastFrameCap = P.Sector == GS->SectorIndex && !P.Grade.bLastFrame ? FMath::Max(Inputs.LastFrameCap, P.Grade.Score) : Inputs.LastFrameCap;
				}
			}
		}
	}
	Record.Grade = DCPhotoRules::GradePhoto(Inputs, UDCTuning::Get().Photo);
	FString GradeTags;
	for (const FName& Cat : Record.Grade.Categories)
	{
		GradeTags += Cat.ToString() + TEXT(",");
	}
	UE_LOG(LogTemp, Display, TEXT("DCPHOTO graded photo=%d shot=%.2f score=%d collapses=%.0f blast=%d factions=%.0f tags=%s"), PhotoId, When, Record.Grade.Score,
		Inputs.Metrics.FindRef(TEXT("Collapses")), Inputs.bHasBlast ? 1 : 0, Inputs.Metrics.FindRef(TEXT("Factions")), *GradeTags);
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

void ADCCharacter::HostApplyHurt(float InAmount, const FVector& From)
{
	// Evidence runs (-dchurtscale=0.2): scripted pilots survive long objective runs; never set in play.
	static float HurtScale = 1.f;
	static const bool bScaled = FParse::Value(FCommandLine::Get(), TEXT("dchurtscale="), HurtScale);
	const float Amount = InAmount * (bScaled ? HurtScale : 1.f);
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
	HostHurtAt = Now();
	ClientHurt(From);
	if (Health <= 0.f)
	{
		UE_LOG(LogTemp, Display, TEXT("DCDOWN %s downed by %.0f from %s (%.0f cm away)"), *GetName(), Amount, *From.ToCompactString(), FVector::Dist(From, GetActorLocation()));
		bDowned = true;
		bLastFrameReady = true; // R21
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
	// R17: surrounded and hit many times a second, the camera stays readable: one capped hit shake per interval and one
	// flinch (hurt flash, sound) per interval; the hit direction still updates every hit.
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	const float Now = GetWorld()->GetRealTimeSeconds();
	LocalFeedback.HurtFrom = From;
	++ShakeLogHits;
	if (Now - LastHitShakeTime >= C.HitShakeMinIntervalSeconds)
	{
		LastHitShakeTime = Now;
		HitTrauma = FMath::Min(HitTrauma + C.HitTraumaPerHit, C.HitTraumaCap);
	}
	if (Now - LocalFeedback.HurtTime < C.FlinchMinIntervalSeconds)
	{
		return;
	}
	LocalFeedback.HurtTime = Now;
	++ShakeLogFlinches;
	DCAudio::Play2D(this, TEXT("Hurt"), 0.7f, FMath::FRandRange(0.95f, 1.05f));
}

void ADCCharacter::ClientToast_Implementation(const FString& Text) { Toast(Text); }
void ADCCharacter::ClientCue_Implementation(FName Cue) { DCAudio::Play2D(this, Cue, 0.9f); }

void ADCCharacter::ClientLastFrameSlowMo_Implementation(float Dilation, float Seconds)
{
	// Presentation only (advisor CP6): this machine's cosmetic clock slows; no world or actor time dilation in co-op,
	// so host and client behave the same and the simulation never drifts.
	if (UDCPresentationClock* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDCPresentationClock>() : nullptr)
	{
		Clock->Begin(Dilation, Seconds);
	}
}


void ADCCharacter::ClientSyncAmmo_Implementation(int32 NewAmmo, int32 NewReserve, uint8 ForWeapon)
{
	if (ForWeapon != WeaponIndex && WeaponStash.IsValidIndex(ForWeapon))
	{
		WeaponStash[ForWeapon] = FIntPoint(NewAmmo, NewReserve); // a weapon not in hand (R8)
		return;
	}
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
		int32 Stunned = 0;
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			if (E->IsAlive() && FVector::Dist(E->GetActorLocation(), View.Location) < Gear->StunRangeCm && DCPhotoProbe::InFrame(View, E->GetActorLocation()))
			{
				E->HostStun(Until);
				++Stunned;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("DCSTUN %s strobe stunned %d enemies for %.1f s"), *GearId.ToString(), Stunned, Gear->StunSeconds);
	}
}

void ADCCharacter::RecordPose()
{
	DC_FRAME_SCOPE("RecordPose");
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
	HostReserve += FMath::RoundToInt(AmmoAdd * UDCTuning::Get().Weapon(HostWeapon).PodAmmoScale);
	Health = FMath::Min(UDCTuning::Get().Move.MaxHealth, Health + Heal);
	if (ADCPlayerState* PS = GetPlayerState<ADCPlayerState>())
	{
		PS->ShotsLeft += Film;
	}
	ClientSyncAmmo(HostAmmo, HostReserve, HostWeapon);
	ClientToast(FString::Printf(TEXT("SUPPLY POD: +%d FILM  +%d AMMO  +%.0f HEALTH"), Film, AmmoAdd, Heal));
	ClientCue(TEXT("Pickup"));
}


void ADCCharacter::OnRep_Downed()
{
	bRifleAim = bRifleAim && !bDowned; // R16: a downed player stops aiming on every machine
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

void ADCCharacter::ServerSetReviving_Implementation(bool bHolding)
{
	bHostReviveHeld = bHolding;
	// R22 cards: a press while standing in a live sector also goes to the owned cards (a turret deploy). The deploy
	// itself refuses near a downed teammate, so a revive keeps the key.
	ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	if (bHolding && !bDowned && GM && PS && GM->IsCombatLive())
	{
		DCUpgrades::Interact(*GM, *PS, *this);
	}
}

void ADCCharacter::HostRevive()
{
	bDowned = false;
	bLastFrameReady = false;
	Health = UDCTuning::Get().Move.MaxHealth * 0.6f;
	ReviveProgress = 0.f;
	OnRep_Downed();
}

void ADCCharacter::HostRefill()
{
	const UDCTuning& T = UDCTuning::Get();
	Health = T.Move.MaxHealth;
	SelfRevivesLeft = 1;
	HostWeaponStash.SetNum(T.NumWeapons());
	for (int32 w = 0; w < T.NumWeapons(); ++w)
	{
		// Every weapon refills (R8); the one in hand is the live counter, the rest sit in the stash.
		const FDCRifleTuning& Full = T.Weapon(w).Stats;
		const FIntPoint Counts(Full.MagazineSize, Full.StartReserve);
		HostWeaponStash[w] = Counts;
		HostAmmo = w == HostWeapon ? Counts.X : HostAmmo;
		HostReserve = w == HostWeapon ? Counts.Y : HostReserve;
		ClientSyncAmmo(Counts.X, Counts.Y, static_cast<uint8>(w));
	}
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
		// The camera sinks into the landing and springs back (Saltline landing spring).
		const FDCMoveTuning& M = UDCTuning::Get().Move;
		LandDipVel -= M.LandDipMaxCm * M.LandDipDamping * FMath::Clamp(Impact / FMath::Max(M.LandDipFullSpeed, 1.f), 0.f, 1.f);
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
	const bool bRifleInHand = Slot == EDCSlot::Rifle;
	RifleMesh->SetRelativeScale3D(FVector(0.75f, 0.08f, 0.12f) * (bRifleInHand ? FMath::Lerp(0.3f, 1.f, Ease) : 1.f));
	const FDCAssetTuning& Assets = UDCTuning::Get().Assets;
	RifleMesh->SetRelativeRotation(bRifleInHand ? Assets.RifleGripRotation + Swing : Assets.HolsterRotation);
	// R16 (human 15:05): while aiming or just after a shot the rifle stays in the hand but points at the aim point (the
	// view's aim: replicated pitch for remote pawns), blended by GunAimBlend, the draw swing on top. Cosmetic, every machine.
	GunShotUntil = MuzzleFlash->IsVisible() ? GetWorld()->GetTimeSeconds() + Assets.AimAfterShotSeconds : GunShotUntil;
	const bool bPoint = bRifleInHand && !IsDowned() && (bRifleAim || GetWorld()->GetTimeSeconds() < GunShotUntil);
	GunAimBlend = FMath::FInterpConstantTo(GunAimBlend, bPoint ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(Assets.AimTurnSeconds, 0.01f));
	if (bRifleInHand && GunAimBlend > 0.f)
	{
		FVector EyeAt;
		FRotator EyeRot;
		GetActorEyesViewPoint(EyeAt, EyeRot);
		// A remote pawn on a client has no controller (its eye rotation is the actor's, pitch 0): use the replicated
		// aim pitch so other players' rifles tilt with their aim (advisor a-53).
		EyeRot.Pitch = IsLocallyControlled() || HasAuthority() ? EyeRot.Pitch : AimPitch;
		const FVector Anchor = EyeAt + EyeRot.Vector() * Assets.AimRangeCm;
		const FQuat Hand = RifleMesh->GetComponentQuat();
		const FQuat Aimed = ((Anchor - RifleMesh->GetComponentLocation()).Rotation() + Swing).Quaternion();
		RifleMesh->SetWorldRotation(FQuat::Slerp(Hand, Aimed, GunAimBlend));
	}
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

void ADCCharacter::HideBodyFromOwner(bool bHide)
{
	// Everything on the body (outfit parts, holstered gear, their voxel children) leaves the first-person view, or a
	// part behind the lens crosses the viewfinder as a big flat polygon (critic V19.6).
	GetMesh()->SetOwnerNoSee(bHide);
	TArray<USceneComponent*> Parts;
	GetMesh()->GetChildrenComponents(true, Parts);
	for (USceneComponent* Part : Parts)
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Part))
		{
			Prim->SetOwnerNoSee(bHide);
		}
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
		HideBodyFromOwner(bFirst);
	}
	FirstCam->SetFieldOfView(FMath::FInterpTo(FirstCam->FieldOfView, LensFOV(), DeltaSeconds, 14.f));

	// Trauma shake: offsets the camera only, never the aim.
	Trauma = FMath::Max(0.f, Trauma - DeltaSeconds * 1.6f);
	HitTrauma = FMath::Max(0.f, HitTrauma - DeltaSeconds * UDCTuning::Get().Camera.HitTraumaDecayPerSecond);
	const float Shake = FMath::Max(Trauma * Trauma, HitTrauma * HitTrauma); // R17: hits never stack onto blast shake
	ShakeLogMax = FMath::Max(ShakeLogMax, HitTrauma);
	if (GetWorld()->GetRealTimeSeconds() >= ShakeLogNext)
	{
		// R17 evidence: hits taken this second against the hit shakes and flinches they produced.
		if (ShakeLogHits > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("DCSHAKE 1s hits=%d flinches=%d hitshake_max=%.2f (cap %.2f) blast=%.2f"), ShakeLogHits, ShakeLogFlinches, ShakeLogMax, UDCTuning::Get().Camera.HitTraumaCap, Trauma);
		}
		ShakeLogNext = GetWorld()->GetRealTimeSeconds() + 1.f;
		ShakeLogHits = ShakeLogFlinches = 0;
		ShakeLogMax = 0.f;
	}
	const float T = GetWorld()->GetRealTimeSeconds();
	const FRotator Jitter(FMath::PerlinNoise1D(T * 23.f) * 4.f * Shake, FMath::PerlinNoise1D(T * 19.f + 7.f) * 4.f * Shake, FMath::PerlinNoise1D(T * 17.f + 3.f) * 3.f * Shake);
	ThirdCam->SetRelativeRotation(Jitter);
	FirstCam->SetRelativeRotation(Jitter * 0.6f);


	// Landing spring and sprint FOV kick (Saltline feel, R9), from the move tuning.
	const FDCMoveTuning& M = UDCTuning::Get().Move;
	const float Accel = -LandDip * M.LandDipStiffness - LandDipVel * M.LandDipDamping;
	LandDipVel += Accel * DeltaSeconds;
	LandDip = FMath::Clamp(LandDip + LandDipVel * DeltaSeconds, -M.LandDipMaxCm, M.LandDipMaxCm);
	// R16: aiming tightens the camera over the shoulder with a short zoom (FOV, boom, offset settle in Aim.ZoomSeconds).
	const FDCAimTuning& AimT = UDCTuning::Get().Aim;
	const float ZoomSpeed = 3.f / FMath::Max(AimT.ZoomSeconds, 0.02f);
	AimBlend = FMath::FInterpTo(AimBlend, bRifleAim ? 1.f : 0.f, DeltaSeconds, ZoomSpeed);
	Boom->SocketOffset = FMath::Lerp(M.ShoulderOffset, AimT.ShoulderOffset, AimBlend) + FVector(0.f, 0.f, LandDip);
	// Downed: the boom lifts above the rubble and pulls back, so the player sees themselves and the street (a boom at
	// knee height in a rubble pile went black).
	const float BoomZ = FMath::FInterpTo(Boom->GetRelativeLocation().Z, bDowned ? M.DownedBoomHeightCm : 40.f, DeltaSeconds, 4.f);
	Boom->SetRelativeLocation(FVector(0.f, 0.f, BoomZ));
	Boom->TargetArmLength = bDowned ? FMath::FInterpTo(Boom->TargetArmLength, M.DownedArmLength, DeltaSeconds, 4.f)
		: FMath::FInterpTo(Boom->TargetArmLength, FMath::Lerp(M.ArmLength, AimT.ArmLength, AimBlend), DeltaSeconds, FMath::Max(4.f, ZoomSpeed));
	const bool bSprintKick = bSprinting && !bCameraRaised && !bRifleAim && GetVelocity().Size2D() > M.WalkSpeed;
	const float HipFOV = M.ThirdPersonFOV + (bSprintKick ? M.SprintFOVKick : 0.f);
	ThirdCam->SetFieldOfView(FMath::FInterpTo(ThirdCam->FieldOfView, bRifleAim ? AimT.AimFOV : HipFOV, DeltaSeconds, bRifleAim ? ZoomSpeed : FMath::Max(6.f, ZoomSpeed)));
	// The owner sends its aim pitch for the others' aim pose (at most ~15 Hz while the rifle is out, only when it moved).
	if (Slot == EDCSlot::Rifle && !bDowned && IsLocallyControlled())
	{
		const float Pitch = FRotator::NormalizeAxis(GetControlRotation().Pitch);
		AimPitch = Pitch;
		if (GetWorld()->GetRealTimeSeconds() >= NextAimPitchSend && FMath::Abs(Pitch - SentAimPitch) > 1.f)
		{
			NextAimPitchSend = GetWorld()->GetRealTimeSeconds() + 1.f / 15.f;
			SentAimPitch = Pitch;
			ServerAimPitch(Pitch);
		}
	}
}

void ADCCharacter::UpdateFallNet()
{
	// R19 (host): remember the last spot where this player stood on the ground inside the sector bounds; anyone below
	// the kill net (fell through a crater edge or past the ring) is put back there, velocity cleared. Never destroyed.
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const FVector At = GetActorLocation();
	if (Move && Move->IsMovingOnGround() && (!City || City->IsInsideBounds(At)) && GetWorld()->GetTimeSeconds() >= NextSafeSample)
	{
		NextSafeSample = GetWorld()->GetTimeSeconds() + 0.5f;
		LastSafeSpot = At;
		bHasSafeSpot = true;
	}
	const bool bOut = City && City->GetBoundsHalfCm() > 0.f && (FMath::Abs(At.X) > City->GetBoundsHalfCm() + 300.f || FMath::Abs(At.Y) > City->GetBoundsHalfCm() + 300.f);
	if (At.Z >= UDCVoxelSettings::Get().Tuning.KillZCm && !bOut)
	{
		return;
	}
	// Past the ring but on the ground: step back just inside it where the player is (a fresh spawn or pod outside the
	// ring has no safe spot in this sector yet). Below the net: the last safe spot.
	const bool bWalkedOut = bOut && At.Z >= UDCVoxelSettings::Get().Tuning.KillZCm;
	const FVector Back = bWalkedOut ? City->ClampInsideBounds(At, UDCVoxelSettings::Get().Tuning.SpawnRingInsetCm) + FVector(0, 0, 60.f)
		: (bHasSafeSpot ? LastSafeSpot + FVector(0, 0, 60.f) : FVector(0, 0, 200.f));
	TeleportTo(Back, GetActorRotation(), false, true);
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	UE_LOG(LogTemp, Display, TEXT("DCKILLZ %s returned from %s to %s (%s)"), *GetName(), *At.ToCompactString(), *Back.ToCompactString(), bOut ? TEXT("past the ring") : TEXT("below the net"));
}

void ADCCharacter::Tick(float DeltaSeconds)
{
	DC_FRAME_SCOPE("Characters");
	Super::Tick(DeltaSeconds);
	TickOutfit(); // every machine builds every pawn's replicated outfit (remote pawns too, CP7)
	UpdateHeldItems(DeltaSeconds);
	UpdateFootsteps(DeltaSeconds);
	UpdateRifleLoco(); // R10: every machine, every pawn (cosmetic)
	if (MuzzleFlash->IsVisible() && GetWorld()->GetTimeSeconds() >= MuzzleHideTime)
	{
		MuzzleFlash->SetVisibility(false);
	}
	if (HasAuthority())
	{
		UpdateRevive(DeltaSeconds);
		RecordPose();
		UpdateFallNet();
		// Health regen by crew size (data, default none): only standing players, only after a quiet spell.
		const FDCMoveTuning& Mv = UDCTuning::Get().Move;
		const AGameStateBase* RegenGS = GetWorld()->GetGameState();
		const int32 Crew = RegenGS ? FMath::Max(1, RegenGS->PlayerArray.Num()) : 1;
		const float RegenRate = Mv.RegenPerSecondByPlayers.Num() > 0 ? Mv.RegenPerSecondByPlayers[FMath::Min(Crew, Mv.RegenPerSecondByPlayers.Num()) - 1] : 0.f;
		if (RegenRate > 0.f && !bDowned && Health < Mv.MaxHealth && Now() - HostHurtAt >= Mv.RegenDelaySeconds)
		{
			Health = FMath::Min(Mv.MaxHealth, Health + RegenRate * DeltaSeconds);
		}
	}
	if (!IsLocallyControlled() || !GetController())
	{
		return;
	}
	UpdateView(DeltaSeconds);
	UpdateStrikeAim();

	const FDCRifleTuning& R = ActiveWeapon().Stats;
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
