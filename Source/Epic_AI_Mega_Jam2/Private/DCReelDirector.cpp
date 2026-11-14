#include "DCReelDirector.h"

#include "DCVoxelCity.h"
#include "Misc/App.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DCAudio.h"
#include "DCPhotoProbe.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCOrbitalStrike.h"
#include "DCOutfitBuild.h"
#include "ProceduralMeshComponent.h"
#include "DCTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Rules/DCCityPlan.h"

namespace
{
	/** Blasts that went off this long before the clip window still show their fireball in it. */
	constexpr float BlastLeadSeconds = 2.f;
	constexpr float OrbitDistanceCm = 520.f;
	constexpr float OrbitHeightCm = 140.f;
	constexpr float OrbitSweepDeg = 40.f;
	constexpr float RunSpeedThreshold = 60.f;

	FDCPoseSample SamplePose(const TArray<FDCPoseSample>& Poses, float T)
	{
		if (Poses.Num() == 0)
		{
			return FDCPoseSample();
		}
		if (T <= Poses[0].Time)
		{
			return Poses[0];
		}
		for (int32 i = 1; i < Poses.Num(); ++i)
		{
			if (Poses[i].Time < T)
			{
				continue;
			}
			const FDCPoseSample& A = Poses[i - 1];
			const FDCPoseSample& B = Poses[i];
			const float U = FMath::Clamp((T - A.Time) / FMath::Max(B.Time - A.Time, 0.001f), 0.f, 1.f);
			FDCPoseSample Out = A;
			Out.Location = FMath::Lerp(FVector(A.Location), FVector(B.Location), U);
			Out.Yaw = A.Yaw + FRotator::NormalizeAxis(B.Yaw - A.Yaw) * U;
			Out.Pitch = FMath::Lerp(A.Pitch, B.Pitch, U);
			Out.Speed = FMath::Lerp(A.Speed, B.Speed, U);
			Out.bCamera = U < 0.5f ? A.bCamera : B.bCamera;
			return Out;
		}
		return Poses.Last();
	}
}

ADCReelDirector::ADCReelDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetReplicates(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	TakeCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("TakeCapture"));
	TakeCapture->SetupAttachment(Camera);
	TakeCapture->bCaptureEveryFrame = false;
	TakeCapture->bCaptureOnMovement = false;
	TakeCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Puppet = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Puppet"));
	Puppet->SetupAttachment(RootComponent);
	Puppet->SetUsingAbsoluteLocation(true);
	Puppet->SetUsingAbsoluteRotation(true);
	Puppet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PuppetCamera = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PuppetCamera"));
	PuppetCamera->SetupAttachment(Puppet);
	PuppetCamera->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PuppetFlash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PuppetFlash"));
	PuppetFlash->SetupAttachment(PuppetCamera);
	PuppetFlash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADCReelDirector::BeginPlay()
{
	Super::BeginPlay();
	const UDCTuning& T = UDCTuning::Get();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(T.Assets.PlayerMesh.TryLoad()))
	{
		Puppet->SetSkeletalMesh(Mesh);
	}
	RunAnim = Cast<UAnimSequence>(T.Assets.ReelRunAnim.TryLoad());
	IdleAnim = Cast<UAnimSequence>(T.Assets.ReelIdleAnim.TryLoad());
	// The stand-in frames with the camera at its eye: a boxy press camera in front of the face reads as
	// "taking the shot" even on the shared aim pose.
	PuppetCamera->AttachToComponent(Puppet, FAttachmentTransformRules::SnapToTargetNotIncludingScale, T.Assets.HeadSocket);
	PuppetCamera->SetStaticMesh(DCAssets::Cube());
	PuppetCamera->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.08f, 0.08f, 0.08f)));
	PuppetCamera->SetRelativeLocation(FVector(0.f, 18.f, 2.f));
	PuppetCamera->SetRelativeScale3D(FVector(0.28f, 0.16f, 0.2f));
	PuppetFlash->AttachToComponent(PuppetCamera, FAttachmentTransformRules::KeepRelativeTransform);
	PuppetFlash->SetStaticMesh(DCAssets::Cube());
	PuppetFlash->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(1.f, 0.95f, 0.8f), 6.f));
	PuppetFlash->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	PuppetFlash->SetRelativeScale3D(FVector(0.5f, 0.6f, 0.4f));
	Puppet->SetVisibility(false, true);
	SetPawnsHidden(true);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTargetWithBlend(this, 0.f);
	}
}

void ADCReelDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearStage();
	if (ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		City->SetFollowGameState(true); // back to the game state's city (cleared after the reel)
	}
	SetPawnsHidden(false);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
		}
	}
	Super::EndPlay(Reason);
}

void ADCReelDirector::SetPawnsHidden(bool bHide)
{
	for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
	{
		C->SetActorHiddenInGame(bHide);
	}
}

void ADCReelDirector::ClearStage()
{
	for (AActor* A : Staged)
	{
		if (IsValid(A))
		{
			A->Destroy();
		}
	}
	Staged.Reset();
	Pending.Reset();
	PendingVoxelOps.Reset();
}

void ADCReelDirector::Restage(int32 ClipIndex)
{
	ClearStage();
	StagedIndex = ClipIndex;
	bAnimSet = false;
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || !GS->ReelClips.IsValidIndex(ClipIndex))
	{
		return;
	}
	const UDCTuning& T = UDCTuning::Get();
	const FDCReelClip& Clip = GS->ReelClips[ClipIndex];
	const int32 Sector = Clip.Photo.Sector;
	ShotTime = Clip.Photo.ShotTime;
	WindowStart = ShotTime - T.Run.ClipSeconds;
	// Clip time t plays at server time ReelClipStart + (t - WindowStart).
	const float Offset = GS->ReelClipStart - WindowStart;

	// The sector's city, rebuilt from the run seed: same towers, same lots.
	const FDCCityPlan Plan = ADCVoxelCity::PlanSector(GS->RunSeed, Sector); // the same specs the sector played with
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TMap<int32, ADCBuilding*> ByLot;
	for (int32 Lot = 0; Lot < Plan.Buildings.Num(); ++Lot)
	{
		const FTransform Xf(Plan.Buildings[Lot].Location);
		ADCBuilding* B = GetWorld()->SpawnActorDeferred<ADCBuilding>(ADCBuilding::StaticClass(), Xf, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		B->SetReplicates(false);
		B->InitSpec(Plan.Buildings[Lot].Spec, Lot);
		B->FinishSpawning(Xf);
		Staged.Add(B);
		ByLot.Add(Lot, B);
	}
	// Voxel city: rebuild the clip's sector from the run seed and replay its ops: the ones before the clip as
	// settled rubble, the ones inside it at their replay time with the fall.
	if (ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		City->SetFollowGameState(false);
		// Reuse the re-staged city when this clip follows the last one in the same sector and nothing already replayed
		// lies after this clip's window start: only the new ops replay (a rebuild plus a full replay froze the reel for
		// seconds in sector 3, R13).
		const int32 Applied = City->GetAppliedSeq();
		bool bReuse = City->GetBuildId() < 0 && City->GetBuiltSector() == Sector && Applied > 0;
		for (const FDCVoxelOp& Op : GS->VoxelOps)
		{
			bReuse &= !(Op.Sector == Sector && Op.Seq <= Applied && Op.Time >= WindowStart);
		}
		if (!bReuse)
		{
			City->Build(GS->RunSeed, Sector, -1 - ClipIndex);
		}
		UE_LOG(LogTemp, Display, TEXT("DCREEL restage clip=%d sector=%d %s (applied seq %d)"), ClipIndex, Sector, bReuse ? TEXT("reused the city") : TEXT("rebuilt the city"), Applied);
		for (const FDCVoxelOp& Op : GS->VoxelOps)
		{
			if (Op.Sector != Sector || Op.Time > ShotTime + 0.5f || (bReuse && Op.Seq <= Applied))
			{
				continue;
			}
			if (Op.Time < WindowStart)
			{
				City->ApplyOp(Op, false);
				continue;
			}
			PendingVoxelOps.Add({Op.Time + Offset, Op});
		}
	}
	for (const FDCCollapseRecord& Rec : GS->Collapses)
	{
		ADCBuilding* const* B = Rec.Sector == Sector ? ByLot.Find(Rec.LotIndex) : nullptr;
		if (!B || Rec.Time > ShotTime + 0.5f)
		{
			continue;
		}
		if (Rec.Time < WindowStart)
		{
			(*B)->RestageCollapse(Rec.Epicenter, false, Rec.Time); // already rubble when the clip starts
			continue;
		}
		Pending.Add({Rec.Time + Offset, *B, Rec.Epicenter, Rec.Time});
	}
	for (const FDCBlastRecord& Blast : GS->Blasts)
	{
		if (Blast.Kind != TEXT("Strike") || Blast.Sector != Sector || Blast.Time < WindowStart - BlastLeadSeconds || Blast.Time > ShotTime + 0.3f)
		{
			continue;
		}
		const FTransform Xf(Blast.Location);
		ADCOrbitalStrike* S = GetWorld()->SpawnActorDeferred<ADCOrbitalStrike>(ADCOrbitalStrike::StaticClass(), Xf, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		S->SetReplicates(false);
		S->InitRestage(Blast.StratagemId, Blast.Time + Offset);
		S->FinishSpawning(Xf);
		Staged.Add(S);
	}
	// The stand-in is the clip photographer's own voxel body (review CP23, R10/R23): the outfit code recorded with
	// the clip; the mannequin only drives the pose.
	const FString Code = Clip.OutfitCode; // recorded with the clip (review CP25: a call sign can be reused)
	DCOutfitBuild::Apply(Puppet, DCOutfitBuild::FromCode(Code), PuppetParts);
	Puppet->SetVisibility(Clip.Poses.Num() > 0, true);
	if (PuppetParts.Num() > 0)
	{
		Puppet->SetVisibility(false, false); // segments show, the mannequin stays hidden
	}
}

void ADCReelDirector::TickPuppet(float ClipTime)
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const FDCReelClip& Clip = GS->ReelClips[StagedIndex];
	if (Clip.Poses.Num() == 0)
	{
		return;
	}
	const FDCPoseSample Pose = SamplePose(Clip.Poses, ClipTime);
	Puppet->SetWorldLocationAndRotation(FVector(Pose.Location) - FVector(0, 0, 92.f), FRotator(0.f, Pose.Yaw - 90.f, 0.f));
	PuppetCamera->SetVisibility(Pose.bCamera, true);
	const bool bRun = Pose.Speed > RunSpeedThreshold;
	UAnimSequence* Want = bRun ? RunAnim.Get() : IdleAnim.Get();
	if (Want && (!bAnimSet || Puppet->GetSingleNodeInstance() == nullptr || bRunning != bRun))
	{
		Puppet->PlayAnimation(Want, true);
		bRunning = bRun;
		bAnimSet = true;
	}
}

void ADCReelDirector::TickCamera(float ClipTime, float Progress)
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const FDCReelClip& Clip = GS->ReelClips[StagedIndex];
	const FVector ShotLoc = Clip.Photo.CameraLocation;
	const FVector Subject = ShotLoc + Clip.Photo.CameraRotation.Vector() * 3000.f;
	if (bTakeRaised)
	{
		// Second take: the player aims, dollies (WASD) and zooms (wheel) freely around the replayed moment.
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			Camera->SetWorldLocationAndRotation(TakeLocation, PC->GetControlRotation());
		}
		Camera->SetFieldOfView(TakeFOV);
		return;
	}
	if (GS->ReelStage > 0 || Clip.Poses.Num() == 0)
	{
		// Frozen at the shutter: the photographer's exact framing.
		Camera->SetWorldLocationAndRotation(ShotLoc, Clip.Photo.CameraRotation);
		Camera->SetFieldOfView(Clip.Photo.FOV);
		return;
	}
	const FDCPoseSample Pose = SamplePose(Clip.Poses, ClipTime);
	const FVector Body = FVector(Pose.Location) + FVector(0, 0, 60.f);
	// Behind-the-scenes orbit: from behind the shoulder, sweeping round to the side as the moment arrives.
	const float Yaw = Pose.Yaw + 180.f - 35.f + OrbitSweepDeg * Progress;
	FVector Eye = Body + FRotator(0.f, Yaw, 0.f).Vector() * OrbitDistanceCm + FVector(0, 0, OrbitHeightCm);
	if (const ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		// Never film from under the street or inside a tower (a buried eye saw only the fog and the ring from below).
		Eye.Z = FMath::Max(Eye.Z, City->SurfaceZCm(Eye) + 150.f);
		for (int32 Step = 0; Step < 12 && City->IsSolidAt(Eye); ++Step)
		{
			Eye = FMath::Lerp(Eye, Body + FVector(0, 0, 250.f), 0.25f);
		}
	}
	const FVector Look = FMath::Lerp(Body, Subject, 0.35f);
	Camera->SetWorldLocationAndRotation(Eye, (Look - Eye).Rotation());
	Camera->SetFieldOfView(70.f);
}

float ADCReelDirector::ContentTime() const
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return GS ? FMath::Clamp(WindowStart + (GS->Now() - GS->ReelClipStart), WindowStart, ShotTime) : 0.f;
}

bool ADCReelDirector::CanTakeThisClip() const
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return GS && GS->Phase == EDCRunPhase::Reel && GS->ReelClips.IsValidIndex(StagedIndex) && GS->ReelStage <= 1 && TakenClip != StagedIndex;
}

void ADCReelDirector::SetTakeRaised(bool bRaised)
{
	if (bRaised == bTakeRaised || (bRaised && !CanTakeThisClip()))
	{
		return;
	}
	bTakeRaised = bRaised;
	if (bRaised)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->SetControlRotation(Camera->GetComponentRotation()); // continue from the orbit's framing
			// Start where the orbit is, inside the take radius around the original camera.
			const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
			const FVector Origin = GS && GS->ReelClips.IsValidIndex(StagedIndex) ? FVector(GS->ReelClips[StagedIndex].Photo.CameraLocation) : Camera->GetComponentLocation();
			TakeLocation = Origin + (Camera->GetComponentLocation() - Origin).GetClampedToMaxSize(UDCTuning::Get().Run.SecondTakeRadiusCm);
			TakeFOV = UDCTuning::Get().Camera.ViewfinderFOVWide;
		}
		DCAudio::Play2D(this, TEXT("Draw"), 0.6f, 1.3f);
	}
}

void ADCReelDirector::DollyTake(const FVector2D& Axes)
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!bTakeRaised || !PC || !GS || !GS->ReelClips.IsValidIndex(StagedIndex))
	{
		return;
	}
	const FDCRunTuning& Run = UDCTuning::Get().Run;
	const FRotator View = PC->GetControlRotation();
	// Real seconds: the reel runs on a dilated clock, the camera crew does not.
	const float Dt = FApp::GetDeltaTime();
	const FVector Move = (FRotationMatrix(View).GetUnitAxis(EAxis::X) * Axes.Y + FRotationMatrix(View).GetUnitAxis(EAxis::Y) * Axes.X) * Run.SecondTakeDollyCmPerSec * Dt;
	const FVector Origin = GS->ReelClips[StagedIndex].Photo.CameraLocation;
	TakeLocation = Origin + (TakeLocation + Move - Origin).GetClampedToMaxSize(Run.SecondTakeRadiusCm);
}

void ADCReelDirector::ZoomTake(float Wheel)
{
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	TakeFOV = FMath::Clamp(TakeFOV - Wheel * C.ZoomStep, C.ViewfinderFOVTight, C.ViewfinderFOVWide);
}

void ADCReelDirector::SnapSecondTake()
{
	ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController());
	const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
	if (!bTakeRaised || !CanTakeThisClip() || !PC || !PS)
	{
		return;
	}
	TakenClip = StagedIndex;
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	UTextureRenderTarget2D* Image = UKismetRenderingLibrary::CreateRenderTarget2D(this, C.PhotoWidth, C.PhotoHeight, RTF_RGBA8);
	TakeCapture->TextureTarget = Image;
	TakeCapture->FOVAngle = Camera->FieldOfView;
	ADCCharacter::ApplyPhotoLook(GetWorld(), TakeCapture->PostProcessSettings, false);
	TakeCapture->PostProcessBlendWeight = 1.f;
	TakeCapture->CaptureScene();
	FDCShotView View;
	View.Location = Camera->GetComponentLocation();
	View.Rotation = Camera->GetComponentRotation();
	View.FOV = Camera->FieldOfView;
	const float When = ContentTime();
	int32 BlastId = 0;
	const FDCPhotoGrade Preview = DCPhotoRules::GradePhoto(DCPhotoProbe::Measure(GetWorld(), View, When, nullptr, false, BlastId), UDCTuning::Get().Photo);
	const int32 PhotoId = (PS->GetPlayerId() & 0xffff) * 1000 + 500 + (++TakeCounter);
	PC->AddLocalPhoto(PhotoId, Image, Preview, View, When);
	PC->RequestSecondTake(PhotoId, View, StagedIndex);
	DCAudio::Play2D(this, TEXT("Shutter"));
	bTakeRaised = false;
}

void ADCReelDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || !GS->ReelClips.IsValidIndex(GS->ReelIndex))
	{
		return;
	}
	if (GS->ReelIndex != StagedIndex)
	{
		bTakeRaised = false;
		Restage(GS->ReelIndex);
	}
	if (bTakeRaised && !CanTakeThisClip())
	{
		bTakeRaised = false;
	}
	const float Now = GS->Now();
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		if (Now >= Pending[i].At)
		{
			if (ADCBuilding* B = Pending[i].Building.Get())
			{
				B->RestageCollapse(Pending[i].Epicenter, true, Pending[i].ContentTime);
			}
			Pending.RemoveAtSwap(i);
		}
	}
	for (int32 i = 0; i < PendingVoxelOps.Num();)
	{
		if (Now < PendingVoxelOps[i].Key)
		{
			break; // kept in op order: apply strictly in sequence
		}
		if (ADCVoxelCity* City = ADCVoxelCity::Get(this))
		{
			City->ApplyOp(PendingVoxelOps[i].Value, true);
		}
		PendingVoxelOps.RemoveAt(0);
	}
	const float Length = FMath::Max(ShotTime - WindowStart, 0.01f);
	const float ClipTime = FMath::Clamp(WindowStart + (Now - GS->ReelClipStart), WindowStart, ShotTime);
	TickPuppet(ClipTime);
	TickCamera(ClipTime, (ClipTime - WindowStart) / Length);
}
