#include "DCAnimShowcase.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DCEnemy.h"
#include "DCTuning.h"
#include "DCUnitAnimator.h"
#include "DCVoxelCity.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	/** The script: from this showcase time on, hold this ground speed; optionally play a clip or die once. */
	struct FCue
	{
		float T;
		float SpeedCm;
		EDCUnitClip Clip;
		bool bDie;
	};
	const FCue Cues[] = {
		{0.f, 0.f, EDCUnitClip::None, false},      // spawn move (the animator starts it) then idle
		{3.5f, 200.f, EDCUnitClip::None, false},   // walk
		{5.5f, 520.f, EDCUnitClip::None, false},   // run
		{7.5f, 0.f, EDCUnitClip::Attack, false},   // attack
		{8.4f, 0.f, EDCUnitClip::Attack, false},
		{9.5f, 0.f, EDCUnitClip::Flinch, false},   // flinch
		{10.5f, 0.f, EDCUnitClip::Flinch, false},
		{11.5f, 0.f, EDCUnitClip::None, true},     // death
	};
	struct FShot
	{
		float T;
		const TCHAR* Label;
	};
	const FShot Shots[] = {
		{0.05f, TEXT("spawn")}, {0.3f, TEXT("spawn")}, {0.6f, TEXT("spawn")}, {0.9f, TEXT("spawn")}, {1.3f, TEXT("spawn")},
		{2.2f, TEXT("idle")}, {3.0f, TEXT("idle")},
		{4.2f, TEXT("walk")}, {4.35f, TEXT("walk")}, {4.5f, TEXT("walk")}, {4.65f, TEXT("walk")}, {4.8f, TEXT("walk")}, {4.95f, TEXT("walk")},
		{6.3f, TEXT("run")}, {6.4f, TEXT("run")}, {6.5f, TEXT("run")}, {6.6f, TEXT("run")}, {6.7f, TEXT("run")}, {6.8f, TEXT("run")},
		{7.53f, TEXT("attack")}, {7.6f, TEXT("attack")}, {7.7f, TEXT("attack")}, {7.85f, TEXT("attack")},
		{9.53f, TEXT("flinch")}, {9.57f, TEXT("flinch")}, {9.63f, TEXT("flinch")}, {9.75f, TEXT("flinch")},
		{11.55f, TEXT("death")}, {11.75f, TEXT("death")}, {11.95f, TEXT("death")}, {12.15f, TEXT("death")}, {12.4f, TEXT("death")},
	};
	/** Fixed step: frames land on exact showcase times however long a screenshot stalls the game. */
	constexpr float Step = 1.f / 30.f;
}

bool UDCAnimShowcase::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), TEXT("dcanimshot="), Unused) && Super::ShouldCreateSubsystem(Outer);
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
	Clock = -1.f;
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
	FString KindList = TEXT("Trooper,Shambler,Raider,Soldier,Skitter");
	FParse::Value(FCommandLine::Get(), TEXT("dcanimkinds="), KindList);
	TArray<FString> Kinds;
	KindList.ParseIntoArray(Kinds, TEXT(","));
	Center = PC->GetPawn()->GetActorLocation();
	Center.Z = City->SurfaceZCm(Center);
	PC->GetPawn()->SetActorHiddenInGame(true);
	const float Spacing = 260.f;
	for (int32 i = 0; i < Kinds.Num(); ++i)
	{
		const FDCEnemyDef* Def = UDCTuning::Get().FindEnemy(FName(*Kinds[i]));
		if (!Def)
		{
			continue;
		}
		const FVector At = Center + FVector((i - (Kinds.Num() - 1) * 0.5f) * Spacing, 0.f, Def->HoverHeight);
		const FTransform Xf(FRotator::ZeroRotator, At);
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
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
	Camera->GetCameraComponent()->SetFieldOfView(70.f);
	const FVector Eye = Center + FVector(0.f, -Spacing * Kinds.Num() * 0.8f, 140.f);
	Camera->SetActorLocationAndRotation(Eye, (Center + FVector(0.f, 0.f, 90.f) - Eye).Rotation());
	Clock = 0.f;
}

void UDCAnimShowcase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || Dir.IsEmpty() || World->GetTimeSeconds() < StartAt)
	{
		return;
	}
	if (Clock < 0.f)
	{
		Start();
		if (Clock < 0.f)
		{
			return;
		}
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC && Camera && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTargetWithBlend(Camera, 0.f);
	}
	const FCue* Cue = nullptr;
	while (NextCue < UE_ARRAY_COUNT(Cues) && Clock >= Cues[NextCue].T)
	{
		Cue = &Cues[NextCue++];
		AttackSeq += Cue->Clip == EDCUnitClip::Attack ? 1 : 0;
	}
	const FCue& Now = Cues[FMath::Max(NextCue - 1, 0)];
	for (ADCEnemy* E : Units)
	{
		UDCUnitAnimator* Anim = E ? E->GetAnimator() : nullptr;
		const FDCEnemyDef* Def = E ? E->GetDef() : nullptr;
		if (!Anim || !Def)
		{
			continue;
		}
		Anim->ForcedSpeedCm = Now.SpeedCm;
		if (Cue && Cue->Clip == EDCUnitClip::Flinch)
		{
			Anim->Play(EDCUnitClip::Flinch);
		}
		Anim->Advance(Step, E->GetActorLocation(), E->GetActorRotation().Yaw, 1.f, AttackSeq, !Now.bDie);
		Anim->ApplyRoot(*Def);
	}
	if (NextShot < UE_ARRAY_COUNT(Shots) && Clock >= Shots[NextShot].T)
	{
		const FShot& S = Shots[NextShot];
		const FString File = Dir / FString::Printf(TEXT("anim_%02d_%s_t%.2f.png"), NextShot, S.Label, S.T);
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT frame %s"), *File);
		++NextShot;
	}
	Clock += Step;
	if (NextShot >= UE_ARRAY_COUNT(Shots) && FParse::Param(FCommandLine::Get(), TEXT("dcanimshotquit")))
	{
		UE_LOG(LogTemp, Display, TEXT("DCANIMSHOT done, quitting"));
		FPlatformMisc::RequestExit(false);
	}
}
