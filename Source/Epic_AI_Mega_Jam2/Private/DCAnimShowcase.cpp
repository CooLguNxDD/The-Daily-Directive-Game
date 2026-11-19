#include "DCAnimShowcase.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "GameFramework/HUD.h"
#include "DCEnemy.h"
#include "DCTuning.h"
#include "DCUnitAnimator.h"
#include "DCVoxelCity.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"

namespace
{
	/** The script: from this showcase time on, hold this ground speed; optionally play a clip or die once. */
	struct FDCAnimShotCue
	{
		float T;
		float SpeedCm;
		EDCUnitClip Clip;
		bool bDie;
	};
	const FDCAnimShotCue AnimShotCues[] = {
		{0.f, 0.f, EDCUnitClip::None, false},      // spawn move (the animator starts it) then idle
		{3.5f, 200.f, EDCUnitClip::None, false},   // walk
		{5.5f, 520.f, EDCUnitClip::None, false},   // run
		{7.5f, 0.f, EDCUnitClip::Attack, false},   // attack
		{8.4f, 0.f, EDCUnitClip::Attack, false},
		{9.5f, 0.f, EDCUnitClip::Flinch, false},   // flinch
		{10.5f, 0.f, EDCUnitClip::Flinch, false},
		{11.5f, 0.f, EDCUnitClip::None, true},     // death
	};
	struct FDCAnimShotFrame
	{
		float T;
		const TCHAR* Label;
	};
	const FDCAnimShotFrame AnimShotFrames[] = {
		{0.05f, TEXT("spawn")}, {0.3f, TEXT("spawn")}, {0.6f, TEXT("spawn")}, {0.9f, TEXT("spawn")}, {1.3f, TEXT("spawn")},
		{2.2f, TEXT("idle")}, {3.0f, TEXT("idle")},
		{4.2f, TEXT("walk")}, {4.35f, TEXT("walk")}, {4.5f, TEXT("walk")}, {4.65f, TEXT("walk")}, {4.8f, TEXT("walk")}, {4.95f, TEXT("walk")},
		{6.3f, TEXT("run")}, {6.4f, TEXT("run")}, {6.5f, TEXT("run")}, {6.6f, TEXT("run")}, {6.7f, TEXT("run")}, {6.8f, TEXT("run")},
		{7.53f, TEXT("attack")}, {7.6f, TEXT("attack")}, {7.7f, TEXT("attack")}, {7.85f, TEXT("attack")},
		{9.53f, TEXT("flinch")}, {9.57f, TEXT("flinch")}, {9.63f, TEXT("flinch")}, {9.75f, TEXT("flinch")},
		{11.55f, TEXT("death")}, {11.75f, TEXT("death")}, {11.95f, TEXT("death")}, {12.15f, TEXT("death")}, {12.4f, TEXT("death")},
	};
	/** Fixed step: frames land on exact showcase times however long a screenshot stalls the game. */
	constexpr float AnimShotStep = 1.f / 30.f;
}

bool UDCAnimShowcase::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Unused;
	int32 Count = 0;
	return (FParse::Value(FCommandLine::Get(), TEXT("dcanimshot="), Unused) || FParse::Value(FCommandLine::Get(), TEXT("dcanimswarm="), Count))
		&& Super::ShouldCreateSubsystem(Outer);
}

void UDCAnimShowcase::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("dcanimshot="), Dir);
	FParse::Value(FCommandLine::Get(), TEXT("dcanimshotat="), StartAt);
	bSwarm = FParse::Value(FCommandLine::Get(), TEXT("dcanimswarm="), SwarmCount) && SwarmCount > 0;
	Clock = -1.f;
}

void UDCAnimShowcase::StartSwarm(int32 Count)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	const ADCVoxelCity* City = ADCVoxelCity::Get(World);
	if (!PC || !PC->GetPawn() || !City)
	{
		return;
	}
	FString KindList = TEXT("Trooper,Swarmer,Shambler,Raider,Soldier,Skitter");
	FParse::Value(FCommandLine::Get(), TEXT("dcanimkinds="), KindList);
	TArray<FString> Kinds;
	KindList.ReplaceInline(TEXT("+"), TEXT(",")); // the command line flag uses + (FParse::Value stops at a comma)
	KindList.ParseIntoArray(Kinds, TEXT(","));
	Center = PC->GetPawn()->GetActorLocation();
	Center.Z = City->SurfaceZCm(Center);
	for (int32 i = 0; i < Count && Kinds.Num() > 0; ++i)
	{
		const FDCEnemyDef* Def = UDCTuning::Get().FindEnemy(FName(*Kinds[i % Kinds.Num()]));
		if (!Def)
		{
			continue;
		}
		// Rings of 10 from 6 m out, 4 m apart: near units pose every frame, far ones at their LOD rate.
		const float Angle = (i % 10) * 36.f + (i / 10) * 18.f;
		const float Radius = 600.f + (i / 10) * 400.f;
		FVector At = Center + FRotator(0.f, Angle, 0.f).Vector() * Radius;
		At.Z = City->SurfaceZCm(At) + Def->HoverHeight;
		const FTransform Xf(FRotator(0.f, Angle + 90.f, 0.f), At);
		ADCEnemy* E = World->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		E->InitKind(Def->Id);
		E->FinishSpawning(Xf);
		E->SetActorTickEnabled(false);
		E->SetCanBeDamaged(false);
		Units.Add(E);
	}
	MakeCapture(Center + FVector(-2600.f, 0.f, 1100.f), Center, 80.f, false);
	UE_LOG(LogTemp, Display, TEXT("DCANIMSWARM spawned %d units round %s, animate=%d"), Units.Num(), *Center.ToCompactString(),
		FParse::Param(FCommandLine::Get(), TEXT("dcanimoff")) ? 0 : 1);
	Clock = 0.f;
}

void UDCAnimShowcase::TickSwarm(float DeltaTime)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("dcanimoff"));
	const double T0 = FPlatformTime::Seconds();
	int32 Alive = 0;
	for (int32 i = 0; i < Units.Num(); ++i)
	{
		ADCEnemy* E = Units[i];
		UDCUnitAnimator* Anim = E ? E->GetAnimator() : nullptr;
		const FDCEnemyDef* Def = E ? E->GetDef() : nullptr;
		if (!Anim || !Def)
		{
			continue;
		}
		++Alive;
		if (bOff)
		{
			continue;
		}
		// Each unit cycles walk, run, idle (6 s, own offset) and attacks every 2 s, at real time.
		static const float Speeds[] = {200.f, 520.f, 0.f};
		Anim->ForcedSpeedCm = Speeds[FMath::Clamp(FMath::FloorToInt32(FMath::Fmod(Clock + i * 0.37f, 6.f) / 2.f), 0, 2)];
		const uint8 Seq = static_cast<uint8>(FMath::FloorToInt32((Clock + i * 0.21f) / 2.f));
		Anim->Advance(DeltaTime, E->GetActorLocation(), E->GetActorRotation().Yaw, 1.f, Seq, true);
		Anim->ApplyRoot(*Def);
	}
	PerfAnimSeconds += FPlatformTime::Seconds() - T0;
	++PerfFrames;
	PerfClock += DeltaTime;
	if (PerfClock >= 1.f)
	{
		UE_LOG(LogTemp, Display, TEXT("DCANIMPERF units=%d animate=%d frames=%d anim_ms_per_frame=%.3f fps=%.1f"), Alive, bOff ? 0 : 1, PerfFrames,
			PerfFrames > 0 ? PerfAnimSeconds * 1000.0 / PerfFrames : 0.0, PerfFrames / PerfClock);
		PerfClock = 0.f;
		PerfAnimSeconds = 0.0;
		PerfFrames = 0;
	}
}

void UDCAnimShowcase::Start()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	const ADCVoxelCity* City = ADCVoxelCity::Get(World);
	if (!PC || !PC->GetPawn() || !City)
	{
		return;
	}
	FString KindList = TEXT("Trooper,Swarmer,Shambler,Raider,Soldier,Skitter");
	FParse::Value(FCommandLine::Get(), TEXT("dcanimkinds="), KindList);
	TArray<FString> Kinds;
	KindList.ReplaceInline(TEXT("+"), TEXT(",")); // the command line flag uses + (FParse::Value stops at a comma)
	KindList.ParseIntoArray(Kinds, TEXT(","));
	Center = PC->GetPawn()->GetActorLocation();
	Center.Z = City->SurfaceZCm(Center);
	PC->GetPawn()->SetActorHiddenInGame(true);
	const float Spacing = 240.f;
	// Lay the line along a clear street axis and put the camera on a clear side (the city has walls everywhere).
	const float Half = Spacing * Kinds.Num() * 0.5f;
	const float CamDist = Spacing * Kinds.Num() * 0.8f;
	const FVector Up(0.f, 0.f, 120.f);
	auto Clear = [City, this, &Up](const FVector& Dir, float Dist) { return !City->IsSightBlocked(Center + Up, Center + Up + Dir * Dist, {}); };
	FVector LineDir = FVector::ForwardVector;
	FVector CamDir = -FVector::RightVector;
	int32 BestScore = -1;
	for (const FVector& Axis : {FVector::ForwardVector, FVector::RightVector})
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Axis);
		for (const FVector& Cam : {Side, -Side})
		{
			const int32 Score = (Clear(Axis, Half) ? 2 : 0) + (Clear(-Axis, Half) ? 2 : 0) + (Clear(Cam, CamDist) ? 3 : 0);
			if (Score > BestScore)
			{
				BestScore = Score;
				LineDir = Axis;
				CamDir = Cam;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT line %s camera side %s clearance score %d/7"), *LineDir.ToCompactString(), *CamDir.ToCompactString(), BestScore);
	for (int32 i = 0; i < Kinds.Num(); ++i)
	{
		const FDCEnemyDef* Def = UDCTuning::Get().FindEnemy(FName(*Kinds[i]));
		if (!Def)
		{
			continue;
		}
		FVector At = Center + LineDir * ((i - (Kinds.Num() - 1) * 0.5f) * Spacing);
		At.Z = City->SurfaceZCm(At) + Def->HoverHeight;
		const FTransform Xf(LineDir.Rotation(), At);
		ADCEnemy* E = World->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		E->InitKind(Def->Id);
		E->FinishSpawning(Xf);
		// Held for the camera: no AI, no damage; the showcase steps the animator itself.
		E->SetActorTickEnabled(false);
		E->SetCanBeDamaged(false);
		Units.Add(E);
		UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT unit %s at %s animator=%d segmented=%d"), *Kinds[i], *At.ToCompactString(),
			E->GetAnimator() ? 1 : 0, E->GetAnimator() && E->GetAnimator()->IsSegmented() ? 1 : 0);
	}
	// Lay the line out by measured size (a Walker is many times a Soldier: frames machines2 showed only its legs), then
	// fit the camera to the whole line and the tallest unit at the 16:9 capture's 70 degree horizontal field of view.
	const float Fov = 70.f;
	TArray<float> Widths;
	float LineLen = 0.f;
	float Tallest = 300.f;
	for (const ADCEnemy* E : Units)
	{
		const FBox B = E->GetComponentsBoundingBox(true);
		const FVector Ext = B.IsValid ? B.GetExtent() : FVector(100.f);
		Widths.Add(FMath::Max(Spacing, 2.f * FMath::Max(Ext.X, Ext.Y) + 80.f));
		LineLen += Widths.Last();
		Tallest = FMath::Max(Tallest, B.IsValid ? B.Max.Z - Center.Z : 300.f);
	}
	float Cursor = -0.5f * LineLen;
	for (int32 i = 0; i < Units.Num(); ++i)
	{
		FVector At = Center + LineDir * (Cursor + 0.5f * Widths[i]);
		At.Z = City->SurfaceZCm(At) + (Units[i]->GetActorLocation().Z - City->SurfaceZCm(Units[i]->GetActorLocation()));
		Units[i]->SetActorLocation(At);
		Cursor += Widths[i];
	}
	const float TanH = FMath::Tan(FMath::DegreesToRadians(0.5f * Fov));
	const float FitDist = FMath::Max(0.55f * LineLen / TanH, 0.6f * Tallest / (TanH * 9.f / 16.f));
	const float Dist = FMath::Max(CamDist * 1.15f, FitDist);
	UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT framing line %.0f cm tallest %.0f cm camera %.0f cm"), LineLen, Tallest, Dist);
	// Frame from the street to above the flyers (critic c-30: the swarmer at hover height was cropped overhead).
	const float EyeZ = FMath::Max(180.f, 0.45f * Tallest);
	const FVector Eye = Center + CamDir * Dist + FVector(0.f, 0.f, EyeZ);
	MakeCapture(Eye, Center + FVector(0.f, 0.f, FMath::Max(150.f, 0.45f * Tallest)), Fov, true);
	// Evidence only: no HUD (no fill light either: the night exposure turns any lit ink unit white, frames anim2/3).
	if (PC->GetHUD())
	{
		PC->GetHUD()->bShowHUD = false;
	}
	Clock = 0.f;
}

void UDCAnimShowcase::MakeCapture(const FVector& Eye, const FVector& LookAt, float Fov, bool bEveryFrame)
{
	UWorld* World = GetWorld();
	ASceneCapture2D* Rig = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform((LookAt - Eye).Rotation(), Eye));
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->InitAutoFormat(1600, 900);
	Target->UpdateResourceImmediate(true);
	Capture = Rig->GetCaptureComponent2D();
	Capture->FOVAngle = Fov;
	Capture->TextureTarget = Target;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Every frame for the lineup, so the capture's own exposure settles like a real view; once per frame shot for the
	// swarm (its perf run must not pay a second scene render).
	Capture->bCaptureEveryFrame = bEveryFrame;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT capture at %s looking %s (eye %s)"), *Capture->GetComponentLocation().ToCompactString(),
		*Capture->GetComponentRotation().ToCompactString(), *Eye.ToCompactString());
}

void UDCAnimShowcase::WriteFrame(const FString& File)
{
	if (!Capture || !Target)
	{
		return;
	}
	// Units the run spawned (not the lineup) stay out of the evidence frame.
	Capture->HiddenActors.Reset();
	for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
	{
		if (!Units.Contains(E))
		{
			Capture->HiddenActors.Add(E);
		}
	}
	Capture->CaptureScene();
	if (NextShot == 0 || NextShot == 20)
	{
		for (const ADCEnemy* E : Units)
		{
			UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT where %s at %s yaw %.0f (capture %s)"), E ? *E->GetKind().ToString() : TEXT("?"),
				E ? *E->GetActorLocation().ToCompactString() : TEXT("-"), E ? E->GetActorRotation().Yaw : 0.f, *Capture->GetComponentLocation().ToCompactString());
		}
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*File));
	const bool bOk = Ar && FImageUtils::ExportRenderTarget2DAsPNG(Target, *Ar);
	UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT frame %s %s"), bOk ? TEXT("wrote") : TEXT("FAILED"), *File);
}

void UDCAnimShowcase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || (Dir.IsEmpty() && !bSwarm) || World->GetTimeSeconds() < StartAt)
	{
		return;
	}
	if (Clock < 0.f)
	{
		if (bSwarm)
		{
			StartSwarm(SwarmCount);
		}
		else
		{
			Start();
		}
		if (Clock < 0.f)
		{
			return;
		}
	}
	if (bSwarm)
	{
		TickSwarm(DeltaTime);
		Clock += DeltaTime;
		if (!Dir.IsEmpty() && NextShot == 0 && Clock >= 3.f)
		{
			WriteFrame(Dir / TEXT("swarm.png"));
			++NextShot;
		}
		return;
	}
	const FDCAnimShotCue* Cue = nullptr;
	while (NextCue < UE_ARRAY_COUNT(AnimShotCues) && Clock >= AnimShotCues[NextCue].T)
	{
		Cue = &AnimShotCues[NextCue++];
		AttackSeq += Cue->Clip == EDCUnitClip::Attack ? 1 : 0;
	}
	const FDCAnimShotCue& Now = AnimShotCues[FMath::Max(NextCue - 1, 0)];
	for (ADCEnemy* E : Units)
	{
		UDCUnitAnimator* Anim = E ? E->GetAnimator() : nullptr;
		const FDCEnemyDef* Def = E ? E->GetDef() : nullptr;
		if (!Anim || !Def)
		{
			continue;
		}
		Anim->ForcedSpeedCm = Now.SpeedCm;
		Anim->bForceFullLod = true;
		if (Cue && Cue->Clip == EDCUnitClip::Flinch)
		{
			Anim->Play(EDCUnitClip::Flinch);
		}
		Anim->Advance(AnimShotStep, E->GetActorLocation(), E->GetActorRotation().Yaw, 1.f, AttackSeq, !Now.bDie);
		Anim->ApplyRoot(*Def);
	}
	if (NextShot < UE_ARRAY_COUNT(AnimShotFrames) && Clock >= AnimShotFrames[NextShot].T)
	{
		const FDCAnimShotFrame& S = AnimShotFrames[NextShot];
		const FString File = Dir / FString::Printf(TEXT("anim_%02d_%s_t%.2f.png"), NextShot, S.Label, S.T);
		WriteFrame(File);
		UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT frame %s"), *File);
		++NextShot;
	}
	Clock += AnimShotStep;
	if (NextShot >= UE_ARRAY_COUNT(AnimShotFrames) && FParse::Param(FCommandLine::Get(), TEXT("dcanimshotquit")))
	{
		UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT done, quitting"));
		FPlatformMisc::RequestExit(false);
	}
}
