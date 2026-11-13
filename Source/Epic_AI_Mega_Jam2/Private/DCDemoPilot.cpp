#include "DCDemoPilot.h"

#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCInput.h"
#include "DCOrbitalStrike.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDCDemo, Log, All);

namespace
{
	constexpr int32 CyclesPerSector = 2;
	/** Seconds after detonation the scripted shutter fires: inside the Peak Cinema window (0.2-0.8 s). */
	constexpr float PeakOffsetSeconds = 0.4f;
}

ADCDemoPilot::ADCDemoPilot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetReplicates(false);
}

bool ADCDemoPilot::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("dcdemo"));
}

void ADCDemoPilot::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Value(FCommandLine::Get(), TEXT("dcshots="), ShotDir))
	{
		ShotDir = FPaths::ProjectSavedDir() / TEXT("DemoShots");
	}
	IFileManager::Get().MakeDirectory(*ShotDir, true);
	bQuit = FParse::Param(FCommandLine::Get(), TEXT("dcdemoquit"));
	BuildScript();
	StepReadyReal = GetWorld()->GetRealTimeSeconds() + 2.0; // let the city, sky and HUD settle
	UE_LOG(LogDCDemo, Display, TEXT("DEMO start: %d steps, shots to %s"), Script.Num(), *ShotDir);
}

UEnhancedInputLocalPlayerSubsystem* ADCDemoPilot::Input() const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
}

const UInputAction* ADCDemoPilot::Action(FName Name) const
{
	const UDCInputSet* Set = UDCInputSet::Get(GetWorld()->GetFirstPlayerController());
	return Set ? Set->Find(Name) : nullptr;
}

void ADCDemoPilot::Press(FName Name, const FVector& Value)
{
	UEnhancedInputLocalPlayerSubsystem* Sub = Input();
	const UInputAction* A = Action(Name);
	if (Sub && A)
	{
		Sub->StartContinuousInputInjectionForAction(A, FInputActionValue(A->ValueType, Value), {}, {});
	}
}

void ADCDemoPilot::Release(FName Name)
{
	UEnhancedInputLocalPlayerSubsystem* Sub = Input();
	const UInputAction* A = Action(Name);
	if (Sub && A)
	{
		Sub->StopContinuousInputInjectionForAction(A);
	}
}

void ADCDemoPilot::Tap(FName Name)
{
	// Held for a few frames so Started and Completed both fire through the real trigger pipeline.
	Press(Name);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Name] { Release(Name); }), 0.08f, false);
}

void ADCDemoPilot::AimAt(const FVector& Target)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector From = PC->PlayerCameraManager->GetCameraLocation();
	PC->SetControlRotation((Target - From).Rotation());
}

void ADCDemoPilot::Shot(const FString& Name)
{
	const FString File = ShotDir / FString::Printf(TEXT("%02d_%s.png"), ShotIndex++, *Name);
	FScreenshotRequest::RequestScreenshot(File, true, false);
	UE_LOG(LogDCDemo, Display, TEXT("DEMO shot %s"), *File);
}

void ADCDemoPilot::LogPhoto()
{
	const ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController());
	const FDCLocalPhoto* P = PC ? PC->GetLastPhoto() : nullptr;
	if (!P)
	{
		UE_LOG(LogDCDemo, Warning, TEXT("DEMO photo: none taken"));
		return;
	}
	TArray<FString> Cats;
	for (const FName& C : P->Grade.Categories)
	{
		Cats.Add(C.ToString());
	}
	UE_LOG(LogDCDemo, Display, TEXT("DEMO photo id=%d confirmed=%d score=%d raw=%d x%.2f timing=%s categories=[%s]"), P->PhotoId,
		P->bConfirmed ? 1 : 0, P->Grade.Score, P->Grade.RawPoints, P->Grade.Multiplier, *P->Grade.TimingLabel.ToString(), *FString::Join(Cats, TEXT(",")));
}

ADCBuilding* ADCDemoPilot::PickTower()
{
	const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Me)
	{
		return nullptr;
	}
	ADCBuilding* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		if (B->IsCollapsed() || UsedTowers.Contains(B))
		{
			continue;
		}
		const float D = FVector::Dist2D(B->GetActorLocation(), Me->GetActorLocation());
		if (D > 2500.f && D < BestDist)
		{
			Best = B;
			BestDist = D;
		}
	}
	if (Best)
	{
		UsedTowers.Add(Best);
	}
	return Best;
}

ADCOrbitalStrike* ADCDemoPilot::LatestStrike() const
{
	ADCOrbitalStrike* Latest = nullptr;
	for (ADCOrbitalStrike* S : TActorRange<ADCOrbitalStrike>(GetWorld()))
	{
		if (!Latest || S->GetDetonateTime() > Latest->GetDetonateTime())
		{
			Latest = S;
		}
	}
	return Latest;
}

void ADCDemoPilot::Add(float Delay, TFunction<bool()> Do, float Timeout)
{
	Script.Add({Delay, MoveTemp(Do), Timeout});
}

void ADCDemoPilot::AddCycle(const FString& C)
{
	// 2: stratagem, aim at the tower's foot, call the strike.
	Add(0.6f, [this] { Tower = PickTower(); return true; });
	// Wait out the strike cooldown (the HUD shows it recharging meanwhile).
	Add(0.f, [this]
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		return !PS || !GS || PS->CooldownReadyTime(TEXT("OrbitalLaser")) <= GS->Now();
	}, 20.f);
	Add(0.f, [this] { Tap(TEXT("Slot2")); return true; });
	Add(0.6f, [this] { if (Tower.IsValid()) { AimAt(Tower->GetActorLocation()); } return true; });
	Add(0.3f, [this, C] { Shot(C + TEXT("_strike_aim")); return true; });
	Add(0.2f, [this] { Tap(TEXT("Fire")); return true; });
	// Run toward it instead of away.
	Add(0.4f, [this, C] { Shot(C + TEXT("_telegraph")); Press(TEXT("Move"), FVector(0, 1, 0)); Press(TEXT("Sprint")); return true; });
	Add(1.0f, [this] { Release(TEXT("Sprint")); Release(TEXT("Move")); return true; });
	// 3: camera out, raise it, frame the tower.
	Add(0.f, [this] { Tap(TEXT("Slot3")); return true; });
	Add(0.5f, [this] { Press(TEXT("Aim")); return true; });
	Add(0.3f, [this, C] { if (Tower.IsValid()) { AimAt(Tower->GetCenter()); } Shot(C + TEXT("_viewfinder")); return true; });
	// Wait for detonation + Peak offset, then snap.
	Add(0.f, [this]
	{
		const ADCOrbitalStrike* S = LatestStrike();
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		if (Tower.IsValid()) { AimAt(Tower->GetCenter()); }
		return !S || !GS || GS->Now() >= S->GetDetonateTime() + PeakOffsetSeconds;
	}, 8.f);
	Add(0.f, [this, C] { Tap(TEXT("Fire")); Shot(C + TEXT("_peak")); return true; });
	Add(0.35f, [this, C] { Shot(C + TEXT("_stamp")); return true; });
	Add(0.6f, [this] { LogPhoto(); return true; });
	Add(0.8f, [this, C] { Shot(C + TEXT("_aftermath")); Release(TEXT("Aim")); return true; });
	Add(0.5f, [this] { Tap(TEXT("Slot1")); return true; });
}

void ADCDemoPilot::AddWaitPhase(EDCRunPhase Phase, float Timeout)
{
	Add(0.f, [this, Phase]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		return GS && GS->Phase == Phase;
	}, Timeout);
}

void ADCDemoPilot::BuildScript()
{
	AddWaitPhase(EDCRunPhase::Sector, 240.f); // co-op scripted runs wait in the lobby for the second player
	Add(0.5f, [this] { Shot(TEXT("spawn")); return true; });

	// Rifle: a short burst at the nearest tower's base.
	Add(0.5f, [this] { Tower = PickTower(); if (Tower.IsValid()) { AimAt(Tower->GetActorLocation() + FVector(0, 0, 300)); } UsedTowers.Reset(); return true; });
	Add(0.3f, [this] { Press(TEXT("Fire")); return true; });
	Add(0.5f, [this] { Shot(TEXT("rifle")); return true; });
	Add(0.6f, [this] { Release(TEXT("Fire")); return true; });

	const int32 Sectors = FMath::Max(1, UDCTuning::Get().Sectors.Num());
	for (int32 Sector = 0; Sector < Sectors; ++Sector)
	{
		const FString S = FString::Printf(TEXT("s%d"), Sector);
		if (Sector > 0)
		{
			AddWaitPhase(EDCRunPhase::Sector, 120.f);
			Add(1.5f, [this, S] { UsedTowers.Reset(); Shot(S + TEXT("_start")); return true; });
		}
		for (int32 Cycle = 0; Cycle < CyclesPerSector; ++Cycle)
		{
			AddCycle(FString::Printf(TEXT("%s_c%d"), *S, Cycle));
		}
		if (Sector + 1 < Sectors)
		{
			// Draft: shot the offer, then click the second card (the camera gear) through the HUD intent.
			AddWaitPhase(EDCRunPhase::Draft, 400.f);
			Add(1.f, [this, S] { Shot(S + TEXT("_draft")); return true; });
			Add(0.5f, [this, Sector]
			{
				ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController());
				const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
				if (PC && PS && PS->DraftOffer.Num() > 0)
				{
					PC->RequestPickUpgrade(PS->DraftOffer[(Sector + 1) % PS->DraftOffer.Num()]);
				}
				return true;
			});
			Add(0.8f, [this, S] { Shot(S + TEXT("_drafted")); return true; });
		}
	}

	// Reel: the clip playing, the freeze, the photo.
	AddWaitPhase(EDCRunPhase::Reel, 400.f);
	Add(2.0f, [this] { Shot(TEXT("reel_play")); return true; });
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->ReelStage == 1; }, 20.f);
	Add(0.3f, [this] { Shot(TEXT("reel_freeze")); return true; });
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->ReelStage == 2; }, 20.f);
	Add(0.6f, [this] { Shot(TEXT("reel_photo")); return true; });

	// Final cut: overrule the critic (take its #2, #3 and #4), then send to press.
	AddWaitPhase(EDCRunPhase::FinalCut, 200.f);
	Add(1.5f, [this] { Shot(TEXT("finalcut")); return true; });
	Add(0.3f, [this]
	{
		ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController());
		const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
		if (PC && PS)
		{
			const int32 N = PS->FinalCutChoices.Num();
			const int32 Want = FMath::Min(UDCTuning::Get().Run.FinalCutPicks, N);
			const int32 Skip = N > Want ? 1 : 0;
			for (int32 i = Skip; i < Skip + Want; ++i)
			{
				PC->ToggleFinalCutPick(PS->FinalCutChoices[i]);
			}
		}
		return true;
	});
	Add(0.5f, [this] { Shot(TEXT("finalcut_picked")); return true; });
	Add(0.3f, [this]
	{
		if (ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			PC->RequestSubmitFinalCut();
		}
		return true;
	});
	AddWaitPhase(EDCRunPhase::FrontPage, 120.f);
	Add(2.f, [this] { Shot(TEXT("frontpage")); return true; });
	Add(1.f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		UE_LOG(LogDCDemo, Display, TEXT("DEMO front page: headline=\"%s\" picks=%d seed=%d"), GS ? *GS->Headline : TEXT(""), GS ? GS->PagePicks.Num() : 0, GS ? GS->RunSeed : 0);
		UE_LOG(LogDCDemo, Display, TEXT("DEMO end"));
		if (bQuit)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
		return true;
	});
}

void ADCDemoPilot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Script.IsValidIndex(Next) || StepReadyReal < 0.0)
	{
		return;
	}
	const double Real = GetWorld()->GetRealTimeSeconds();
	if (Real < StepReadyReal)
	{
		return;
	}
	if (StepStartReal < 0.0)
	{
		StepStartReal = Real;
	}
	FStep& Step = Script[Next];
	const bool bDone = Step.Do();
	if (!bDone && Real - StepStartReal < Step.Timeout)
	{
		return;
	}
	if (!bDone)
	{
		UE_LOG(LogDCDemo, Warning, TEXT("DEMO step %d timed out"), Next);
	}
	++Next;
	StepStartReal = -1.0;
	StepReadyReal = Real + (Script.IsValidIndex(Next) ? Script[Next].Delay : 0.f);
}
