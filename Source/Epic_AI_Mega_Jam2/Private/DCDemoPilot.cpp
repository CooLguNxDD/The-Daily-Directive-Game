#include "DCDemoPilot.h"
#include "DCAudio.h"

#include "DCBuilding.h"
#include "DCEnemy.h"
#include "DCSupplyPod.h"
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

bool ADCDemoPilot::IsMedicRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("dcmedic"));
}

void ADCDemoPilot::TickMedic()
{
	ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
	if (!Me || Me->IsDowned())
	{
		return;
	}
	ADCCharacter* Down = nullptr;
	for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
	{
		Down = (C != Me && C->IsDowned()) ? C : Down;
	}
	if (!Down)
	{
		if (bMedicHolding)
		{
			bMedicHolding = false;
			Release(TEXT("Interact"));
			Shot(TEXT("medic_revived_teammate"));
			UE_LOG(LogDCDemo, Display, TEXT("DEMO medic: teammate back up"));
		}
		Release(TEXT("Move"));
		return;
	}
	const float Dist = FVector::Dist2D(Down->GetActorLocation(), Me->GetActorLocation());
	AimAt(Down->GetActorLocation());
	if (Dist > 220.f)
	{
		Press(TEXT("Move"), FVector(0, 1, 0)); // run to them
		return;
	}
	Release(TEXT("Move"));
	if (!bMedicHolding)
	{
		bMedicHolding = true;
		Press(TEXT("Interact"));
		Shot(TEXT("medic_holding_E_on_teammate"));
		UE_LOG(LogDCDemo, Display, TEXT("DEMO medic: holding E on %s"), *GetNameSafe(Down->GetPlayerState()));
	}
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
	bMedic = IsMedicRequested() && !IsRequested();
	if (bMedic)
	{
		return;
	}
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
	Add(0.2f, [this, C] { Shot(C + TEXT("_draw_stratagem")); return true; });
	FString WantedStratagem;
	if (!C.StartsWith(TEXT("s0")) && FParse::Value(FCommandLine::Get(), TEXT("dcstratagem="), WantedStratagem))
	{
		// -dcstratagem=Id: wait out its own cooldown, Q cycles until it is selected (R10 evidence), then log and frame it.
		Add(0.f, [this, WantedStratagem]
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
			const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
			return !PS || !GS || PS->CooldownReadyTime(FName(*WantedStratagem)) <= GS->Now();
		}, 35.f);
		for (int32 i = 0; i < 6; ++i)
		{
			Add(0.35f, [this, WantedStratagem]
			{
				const APlayerController* PC = GetWorld()->GetFirstPlayerController();
				const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
				if (Me && Me->GetSelectedStratagem() != FName(*WantedStratagem))
				{
					Tap(TEXT("Cycle"));
				}
				return true;
			});
		}
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
	else if (C.Contains(TEXT("_c1")) && !C.StartsWith(TEXT("s0")))
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
	Add(0.2f, [this, C] { Shot(C + TEXT("_draw_camera")); return true; });
	Add(0.3f, [this] { Press(TEXT("Aim")); return true; });
	Add(0.3f, [this, C] { if (Tower.IsValid()) { AimAt(Tower->GetCenter()); } Shot(C + TEXT("_viewfinder")); return true; });
	// Wait for detonation + Peak offset, then snap.
	Add(0.f, [this]
	{
		const ADCOrbitalStrike* S = LatestStrike();
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		if (Tower.IsValid()) { AimAt(Tower->GetCenter()); }
		// -dcpeakoffset= moves the shutter (0.25 lands it while the strike's voxel op is often still on the worker).
		float Offset = PeakOffsetSeconds;
		FParse::Value(FCommandLine::Get(), TEXT("dcpeakoffset="), Offset);
		return !S || !GS || GS->Now() >= S->GetDetonateTime() + Offset;
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

void ADCDemoPilot::AddWidestFrame(const FString& S)
{
	// The widest the lens goes (wheel all the way out), framed at each sector start: 1.3x on the stock lens,
	// wider with Fisheye gear. Then back to the default zoom.
	Add(0.f, [this] { Tap(TEXT("Slot3")); return true; });
	Add(0.7f, [this] { Press(TEXT("Aim")); Press(TEXT("Zoom"), FVector(-1, 0, 0)); return true; });
	Add(0.6f, [this] { Release(TEXT("Zoom")); Tower = PickTower(); if (Tower.IsValid()) { AimAt(Tower->GetCenter()); } return true; });
	Add(0.4f, [this, S] { Shot(S + TEXT("_widest")); UsedTowers.Reset(); return true; });
	Add(0.f, [this] { Press(TEXT("Zoom"), FVector(1, 0, 0)); return true; });
	Add(0.12f, [this] { Release(TEXT("Zoom")); Release(TEXT("Aim")); Tap(TEXT("Slot1")); return true; });
}

void ADCDemoPilot::AddStratagemOnSwarm(const FString& S)
{
	// -dcstratagem=Id (sectors 2-3): drop the drafted stratagem on the swarm, then watch it through the viewfinder
	// (Decoy: the swarm mobs the beacon; Singularity: the swarm is dragged in). Frames every second.
	FString Wanted;
	if (S == TEXT("s0") || !FParse::Value(FCommandLine::Get(), TEXT("dcstratagem="), Wanted))
	{
		return;
	}
	auto NearestEnemy = [this]() -> ADCEnemy*
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		ADCEnemy* Best = nullptr;
		float BestD = 1e9f;
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			const float D = Me && E->IsAlive() && !E->IsWalker() ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : 1e9f;
			Best = D < BestD ? E : Best;
			BestD = FMath::Min(BestD, D);
		}
		return Best;
	};
	Add(0.f, [this, Wanted]
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		return !PS || !GS || PS->CooldownReadyTime(FName(*Wanted)) <= GS->Now();
	}, 35.f);
	Add(0.f, [NearestEnemy] { return NearestEnemy() != nullptr; }, 30.f);
	Add(0.f, [this] { Tap(TEXT("Slot2")); return true; });
	for (int32 i = 0; i < 6; ++i)
	{
		Add(0.35f, [this, Wanted]
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
			if (Me && Me->GetSelectedStratagem() != FName(*Wanted))
			{
				Tap(TEXT("Cycle"));
			}
			return true;
		});
	}
	Add(0.3f, [this, NearestEnemy]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (ADCEnemy* E = NearestEnemy())
		{
			// 20 m toward the swarm, on the street at my feet' height, so the crowd gathers in front of the lens.
			const FVector From = Me ? Me->GetActorLocation() : E->GetActorLocation();
			const FVector Dir = (E->GetActorLocation() - From).GetSafeNormal2D();
			AimAt(From + Dir * 2000.f - FVector(0, 0, 90.f));
		}
		return true;
	});
	Add(0.3f, [this, S] { Tap(TEXT("Fire")); Shot(S + TEXT("_crowd_call")); return true; });
	Add(0.5f, [this] { Tap(TEXT("Slot3")); return true; });
	// Widest lens for the crowd, frames every half second through the telegraph.
	Add(0.6f, [this] { Press(TEXT("Aim")); Press(TEXT("Zoom"), FVector(-1, 0, 0)); return true; });
	Add(0.5f, [this] { Release(TEXT("Zoom")); return true; });
	for (int32 i = 0; i < 12; ++i)
	{
		Add(i == 0 ? 0.1f : 0.5f, [this, S, i]
		{
			for (ADCOrbitalStrike* Strike : TActorRange<ADCOrbitalStrike>(GetWorld()))
			{
				AimAt(Strike->GetActorLocation() + FVector(0, 0, 150.f));
			}
			Shot(FString::Printf(TEXT("%s_crowd_%d"), *S, i));
			return true;
		});
	}
	Add(0.2f, [this] { Release(TEXT("Aim")); Tap(TEXT("Slot1")); return true; });
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
	Add(0.3f, [this] { Release(TEXT("Fire")); return true; });
	// Camera on the swarm: the BRUTALITY tag in the viewfinder, the shot, and (with Strobe Flash) the stun after it.
	Add(0.f, [this] { Tap(TEXT("Slot3")); return true; });
	Add(0.7f, [this] { Press(TEXT("Aim")); return true; });
	Add(0.4f, [this, S]
	{
		if (Target.IsValid() && Target->IsAlive())
		{
			AimAt(Target->GetActorLocation());
		}
		Shot(S + TEXT("_swarm_viewfinder"));
		return true;
	});
	// Let the swarmer close in (rush on raise) so it fills the frame, up to 4 s, tracking it.
	Add(0.f, [this]
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const APawn* Me = PC ? PC->GetPawn() : nullptr;
		if (!Me)
		{
			return true;
		}
		float Best = 1e9f;
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			const float D = E->IsAlive() ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : 1e9f;
			Target = D < Best ? E : Target.Get();
			Best = FMath::Min(Best, D);
		}
		if (Target.IsValid() && Target->IsAlive())
		{
			AimAt(Target->GetActorLocation());
		}
		return Best < 3200.f; // inside Strobe range (40 m), still small enough to frame
	}, 6.f);
	Add(0.f, [this, S] { Shot(S + TEXT("_swarm_closing")); Tap(TEXT("Fire")); return true; });
	Add(0.35f, [this, S] { Shot(S + TEXT("_swarm_stamp")); LogPhoto(); return true; });
	Add(0.9f, [this, S] { Shot(S + TEXT("_swarm_after")); Release(TEXT("Aim")); return true; });
	Add(0.2f, [this] { Tap(TEXT("Slot1")); Target.Reset(); return true; });
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

void ADCDemoPilot::AddRifleOnly()
{
	// R6 play test (-dcrifleonly): a whole sector held with the rifle only, no strikes and no photos. Every 15 s the
	// pilot logs health, ammo, enemies alive and the quota; downed, it holds E for its one self-revive.
	for (int32 i = 0; i < 20; ++i)
	{
		Add(i == 0 ? 0.f : 15.f, [this, i]
		{
			bDefend = true;
			const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
			const ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
			int32 Alive = 0;
			float Nearest = -1.f;
			for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
			{
				Alive += E->IsAlive() ? 1 : 0;
				const float D = E->IsAlive() && Me ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : -1.f;
				Nearest = D >= 0.f && (Nearest < 0.f || D < Nearest) ? D : Nearest;
			}
			const int32 SectorIdx = GS ? GS->SectorIndex : -1;
			const int32 Quota = UDCTuning::Get().Sectors.IsValidIndex(SectorIdx) ? UDCTuning::Get().Sectors[SectorIdx].QuotaPoints : 0;
			// The nearest swarmer: where it is relative to me (a stuck pack shows here).
			const ADCEnemy* NearSwarmer = nullptr;
			float NearSwarmerD = 1e9f;
			for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
			{
				const float D = Me && E->IsAlive() && E->GetKind() == TEXT("Swarmer") ? FVector::Dist2D(E->GetActorLocation(), Me->GetActorLocation()) : 1e9f;
				NearSwarmer = D < NearSwarmerD ? E : NearSwarmer;
				NearSwarmerD = FMath::Min(NearSwarmerD, D);
			}
			if (NearSwarmer && Me)
			{
				UE_LOG(LogDCDemo, Display, TEXT("DEMO rifle-only swarmer d2d=%.0fm dz=%.0fm loc=%s"), NearSwarmerD / 100.f,
					(NearSwarmer->GetActorLocation().Z - Me->GetActorLocation().Z) / 100.f, *NearSwarmer->GetActorLocation().ToCompactString());
			}
			UE_LOG(LogDCDemo, Display, TEXT("DEMO rifle-only t=%ds phase=%d health=%.0f ammo=%d downed=%d enemies=%d nearest=%.0fm quota=%d/%d failed=%d"), i * 15,
				GS ? static_cast<int32>(GS->Phase) : -1, Me ? Me->GetHealth() : -1.f, Me ? Me->GetAmmo() : -1, Me && Me->IsDowned() ? 1 : 0, Alive, Nearest / 100.f,
				GS ? GS->SectorPhotoPoints : 0, Quota, GS && GS->bRunFailed ? 1 : 0);
			if (Me && Me->IsDowned())
			{
				Press(TEXT("Interact"));
			}
			else
			{
				Release(TEXT("Interact"));
			}
			Shot(FString::Printf(TEXT("rifleonly_%03ds"), i * 15));
			return true;
		});
	}
	Add(0.f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		UE_LOG(LogDCDemo, Display, TEXT("DEMO rifle-only end: phase=%d failed=%d quota results=%d met=%d"), GS ? static_cast<int32>(GS->Phase) : -1,
			GS && GS->bRunFailed ? 1 : 0, GS ? GS->QuotaResults.Num() : 0, GS && GS->QuotaResults.Num() > 0 && GS->QuotaResults.Last() ? 1 : 0);
		UE_LOG(LogDCDemo, Display, TEXT("DEMO end"));
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->StopEvidenceRecording(this);
		}
		if (bQuit)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
		return true;
	});
}

void ADCDemoPilot::AddCueRoll()
{
	// Listening evidence (-dccueroll): every cue in the bank once, alone over a muted music bed, then the music.
	Add(0.f, [this]
	{
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->StartEvidenceRecording(this);
			Audio->SetMusicVolume(0.f);
		}
		return true;
	});
	for (const FName& Cue : DCAudio::CueNames())
	{
		const UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>();
		const float Gap = Audio ? FMath::Min(Audio->CueSeconds(Cue), 3.f) + 0.5f : 1.f;
		Add(0.f, [this, Cue] { DCAudio::Play2D(this, Cue); return true; });
		Add(Gap, [] { return true; });
	}
	Add(0.f, [this]
	{
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->SetMusicVolume(1.f);
		}
		return true;
	});
}

void ADCDemoPilot::BuildScript()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("dccueroll")))
	{
		AddCueRoll();
	}
	Add(0.f, [this]
	{
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->StartEvidenceRecording(this); // no-op without -dcaudiorec, or when the cue roll started it
		}
		return true;
	});
	AddWaitPhase(EDCRunPhase::Sector, 240.f); // co-op scripted runs wait in the lobby for the second player
	Add(0.5f, [this] { Shot(TEXT("spawn")); return true; });
	if (FParse::Param(FCommandLine::Get(), TEXT("dcrifleonly")))
	{
		AddRifleOnly();
		return;
	}

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
			AddWidestFrame(S);
			AddStratagemOnSwarm(S); // -dcstratagem only: the drafted stratagem on the first pack, cooldown fresh
		}
		int32 Cycles = CyclesPerSector;
		FParse::Value(FCommandLine::Get(), TEXT("dccycles="), Cycles); // more strike-and-snap cycles per sector
		for (int32 Cycle = 0; Cycle < Cycles; ++Cycle)
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
	Add(1.2f, [this] { Shot(TEXT("reel_play")); return true; });
	// Second take (R16): raise the camera during the replay, dolly sideways (WASD), frame the moment, snap.
	Add(0.f, [this] { Press(TEXT("Aim")); return true; });
	Add(0.1f, [this] { Press(TEXT("Move"), FVector(1, 0.4f, 0)); return true; });
	Add(0.7f, [this] { Release(TEXT("Move")); return true; });
	Add(0.3f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		if (GS && GS->ReelClips.IsValidIndex(GS->ReelIndex))
		{
			const FDCPhotoRecord& P = GS->ReelClips[GS->ReelIndex].Photo;
			AimAt(FVector(P.CameraLocation) + P.CameraRotation.Vector() * 2500.f);
		}
		Shot(TEXT("reel_second_take_raised"));
		return true;
	});
	Add(0.3f, [this] { Tap(TEXT("Fire")); return true; });
	Add(0.4f, [this] { Shot(TEXT("reel_second_take_stamp")); Release(TEXT("Aim")); return true; });
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
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->StopEvidenceRecording(this);
		}
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
	if (bMedic)
	{
		TickMedic();
		return;
	}
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
		LastFrameAtReal = bDown ? Real + 0.6 : -1.0;
	}
	// R21 evidence: the downed pilot takes its last frame from the ground (the real Fire action), then frames it.
	if (bDown && Me->HasLastFrame() && LastFrameAtReal > 0.0 && Real >= LastFrameAtReal)
	{
		LastFrameAtReal = -1.0;
		Tap(TEXT("Fire"));
		UE_LOG(LogDCDemo, Display, TEXT("DEMO last frame snapped"));
		FTimerHandle Later;
		GetWorldTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(this, [this] { Shot(TEXT("last_frame")); }), 0.4f, false);
	}
	TickDefend();
	// The first time a spotlight unit lights the pilot, frame the blow-out (R11 evidence).
	if (Me && Me->IsSpotlit() && !bSpotlitFramed)
	{
		bSpotlitFramed = true;
		Shot(TEXT("spotlit_blown_out"));
		UE_LOG(LogDCDemo, Display, TEXT("DEMO spotlit"));
	}
	// Any supply pod that lands gets framed once (HQ Scoop reward evidence).
	for (ADCSupplyPod* Pod : TActorRange<ADCSupplyPod>(GetWorld()))
	{
		if (!FramedPods.Contains(Pod) && Pod->HasLanded())
		{
			FramedPods.Add(Pod);
			AimAt(Pod->GetActorLocation() + FVector(0, 0, 100.f));
			Shot(TEXT("supply_pod_landed"));
			UE_LOG(LogDCDemo, Display, TEXT("DEMO supply pod landed"));
		}
	}
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
