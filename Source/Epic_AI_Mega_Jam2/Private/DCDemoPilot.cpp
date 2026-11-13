#include "DCDemoPilot.h"

#include "DCBuilding.h"
#include "DCEnemy.h"
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

void ADCDemoPilot::AimAt(const FVector& LookTarget)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector From = PC->PlayerCameraManager->GetCameraLocation();
	PC->SetControlRotation((LookTarget - From).Rotation());
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
	if (C.Contains(TEXT("_c1")) && !C.StartsWith(TEXT("s0")))
	{
		// Spend Propaganda Points: Q cycles to the 600-point Barrage (R6).
		Add(0.5f, [this] { Tap(TEXT("Cycle")); return true; });
		Add(0.3f, [this, C]
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
			const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
			UE_LOG(LogDCDemo, Display, TEXT("DEMO stratagem selected=%s points=%d"), Me ? *Me->GetSelectedStratagem().ToString() : TEXT("?"), PS ? PS->Points : -1);
			Shot(C + TEXT("_stratagem_selected"));
			return true;
		});
	}
	Add(0.6f, [this] { if (Tower.IsValid()) { AimAt(Tower->GetActorLocation()); } return true; });
	Add(0.3f, [this, C] { Shot(C + TEXT("_strike_aim")); return true; });
	Add(0.2f, [this] { Tap(TEXT("Fire")); return true; });
	Add(0.3f, [this]
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
		UE_LOG(LogDCDemo, Display, TEXT("DEMO strike called: points now=%d"), PS ? PS->Points : -1);
		return true;
	});
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

void ADCDemoPilot::AddWalkerWatch()
{
	auto FindWalker = [this]() -> ADCEnemy*
	{
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			if (E->IsAlive() && E->IsWalker())
			{
				return E;
			}
		}
		return nullptr;
	};
	Add(0.f, [this, FindWalker] { bDefend = true; return FindWalker() != nullptr; }, 120.f);
	Add(0.f, [this] { bDefend = false; Release(TEXT("Fire")); bDefendFiring = false; Tap(TEXT("Slot3")); return true; });
	Add(0.6f, [this] { Press(TEXT("Aim")); return true; });
	for (int32 i = 0; i < 3; ++i)
	{
		Add(0.6f, [this, FindWalker, i]
		{
			if (ADCEnemy* W = FindWalker())
			{
				AimAt(W->GetActorLocation());
			}
			Shot(FString::Printf(TEXT("walker_view%d"), i));
			return true;
		});
	}
	Add(0.2f, [this] { Tap(TEXT("Fire")); return true; });
	Add(0.4f, [this] { Shot(TEXT("walker_snap")); LogPhoto(); return true; });
	// Wait until the walker condemns a tower, then frame the shaking tower and snap its fall.
	Add(0.f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		const FDCBlastRecord* Topple = GS ? GS->Blasts.FindByPredicate([](const FDCBlastRecord& R) { return R.Kind == TEXT("Topple"); }) : nullptr;
		if (Topple)
		{
			AimAt(Topple->Location + FVector(0, 0, 1200.f));
		}
		return Topple != nullptr && GS->Now() >= Topple->TelegraphStart + 0.5f;
	}, 120.f);
	Add(0.f, [this] { Shot(TEXT("walker_condemned_tower")); return true; });
	Add(0.f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		const FDCBlastRecord* Topple = GS ? GS->Blasts.FindByPredicate([](const FDCBlastRecord& R) { return R.Kind == TEXT("Topple"); }) : nullptr;
		return !Topple || GS->Now() >= Topple->Time + 0.4f;
	}, 10.f);
	Add(0.f, [this] { Tap(TEXT("Fire")); Shot(TEXT("walker_topple_peak")); return true; });
	Add(0.4f, [this] { Shot(TEXT("walker_topple_stamp")); LogPhoto(); Release(TEXT("Aim")); return true; });
}

void ADCDemoPilot::AddEnemyFight(const FString& S)
{
	// Wait for an enemy, then hold the rifle on it: frames of the enemy, the tracer and the hit marker.
	Add(0.f, [this] { Tap(TEXT("Slot1")); return true; });
	Add(0.6f, [this]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		if (!Me || !GS || GS->Phase != EDCRunPhase::Sector)
		{
			return true; // the sector ended first
		}
		float Best = TNumericLimits<float>::Max();
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			const float D = E->IsAlive() ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : Best;
			if (D < Best)
			{
				Best = D;
				Target = E;
			}
		}
		return Target.IsValid();
	}, 40.f);
	for (int32 i = 0; i < 4; ++i)
	{
		Add(i == 0 ? 0.f : 0.5f, [this, S, i]
		{
			if (Target.IsValid())
			{
				AimAt(Target->GetActorLocation());
				Press(TEXT("Fire"));
			}
			Shot(FString::Printf(TEXT("%s_enemy%d"), *S, i));
			return true;
		});
	}
	Add(0.3f, [this] { Release(TEXT("Fire")); Target.Reset(); return true; });
	// Downed: hold E through the real input path to self-revive (solo), with frames before and after.
	Add(0.2f, [this, S]
	{
		const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
		if (Me && Me->IsDowned())
		{
			Shot(S + TEXT("_downed"));
			Press(TEXT("Interact"));
		}
		return true;
	});
	Add(0.f, [this]
	{
		const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
		return !Me || !Me->IsDowned();
	}, 8.f);
	Add(0.3f, [this, S]
	{
		Release(TEXT("Interact"));
		const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
		UE_LOG(LogDCDemo, Display, TEXT("DEMO revive check: downed=%d"), Me && Me->IsDowned() ? 1 : 0);
		Shot(S + TEXT("_after_revive_check"));
		return true;
	});
}

void ADCDemoPilot::AddWaitPhase(EDCRunPhase Phase, float Timeout)
{
	Add(0.f, [this, Phase]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		bDefend = !(GS && GS->Phase == Phase);
		return !bDefend;
	}, Timeout);
}

void ADCDemoPilot::TickDefend()
{
	const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCEnemy* Nearest = nullptr;
	float Best = 2500.f;
	if (bDefend && Me && !Me->IsDowned() && GS && GS->Phase == EDCRunPhase::Sector)
	{
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			const float D = E->IsAlive() ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : Best;
			Nearest = D < Best ? E : Nearest;
			Best = FMath::Min(Best, D);
		}
	}
	if (Nearest)
	{
		if (Me->GetSlot() != EDCSlot::Rifle)
		{
			Tap(TEXT("Slot1"));
		}
		AimAt(Nearest->GetActorLocation());
	}
	if ((Nearest != nullptr) != bDefendFiring)
	{
		bDefendFiring = Nearest != nullptr;
		(bDefendFiring ? Press(TEXT("Fire")) : Release(TEXT("Fire")));
	}
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

	if (FParse::Param(FCommandLine::Get(), TEXT("dcwatchwalker")))
	{
		AddWalkerWatch();
		Add(1.f, [this]
		{
			UE_LOG(LogDCDemo, Display, TEXT("DEMO end"));
			if (bQuit)
			{
				FGenericPlatformMisc::RequestExit(false);
			}
			return true;
		});
		return;
	}
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
		AddEnemyFight(S);
		if (Sector + 1 < Sectors)
		{
			// Draft: shot the offer, then click the second card (the camera gear) through the HUD intent.
			// Draft, or straight into the next sector when the crew missed HQ's quota.
			Add(0.f, [this, Sector]
			{
				const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
				bDefend = !(GS && (GS->Phase == EDCRunPhase::Draft || GS->Phase == EDCRunPhase::Reel || GS->SectorIndex > Sector));
				return !bDefend;
			}, 400.f);
			Add(1.f, [this, S] { Shot(S + TEXT("_draft")); return true; });
			Add(0.5f, [this, Sector]
			{
				ADCPlayerController* PC = Cast<ADCPlayerController>(GetWorld()->GetFirstPlayerController());
				const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
				if (PC && PS && PS->DraftOffer.Num() > 0)
				{
					// -dcdraft=A,B picks the first listed upgrade on offer; otherwise a fixed card per sector.
					FString Wanted;
					TArray<FString> Prefs;
					if (FParse::Value(FCommandLine::Get(), TEXT("dcdraft="), Wanted, false))
					{
						Wanted.ParseIntoArray(Prefs, TEXT(","));
					}
					const FString* Hit = Prefs.FindByPredicate([PS](const FString& P) { return PS->DraftOffer.Contains(FName(*P)); });
					PC->RequestPickUpgrade(Hit ? FName(**Hit) : PS->DraftOffer[(Sector + 1) % PS->DraftOffer.Num()]);
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
	// Whenever the pilot is downed, it holds E (the real Interact action) to self-revive, framing both ends.
	const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
	const bool bDown = Me && Me->IsDowned();
	if (bDown != bAutoReviving)
	{
		bAutoReviving = bDown;
		Shot(bDown ? TEXT("downed_holding_E") : TEXT("revived"));
		UE_LOG(LogDCDemo, Display, TEXT("DEMO %s"), bDown ? TEXT("downed: holding E") : TEXT("revived"));
		(bDown ? Press(TEXT("Interact")) : Release(TEXT("Interact")));
	}
	TickDefend();
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
