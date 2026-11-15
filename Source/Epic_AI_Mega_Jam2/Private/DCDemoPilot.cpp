#include "DCDemoPilot.h"
#include "DCFrameScopes.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DCObjective.h"
#include "DCUnits.h"
#include "DCVoxelCity.h"
#include "Rules/DCFactionRules.h"
#include "DCAudio.h"
#include "DCMusic.h"

#include "DCBuilding.h"
#include "DCEnemy.h"
#include "DCSupplyPod.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCGameMode.h"
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
		// -dccostar (R20 evidence): stand in the host's shot, between the host and a telegraphing strike, CoStarSpotCm
		// short of the blast, so the host's photo frames a teammate near the blast.
		static const bool bCoStar = FParse::Param(FCommandLine::Get(), TEXT("dccostar"));
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		ADCCharacter* Host = nullptr;
		for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
		{
			Host = C != Me ? C : Host;
		}
		const ADCOrbitalStrike* Strike = nullptr;
		for (ADCOrbitalStrike* S : TActorRange<ADCOrbitalStrike>(GetWorld()))
		{
			Strike = GS && S->GetDetonateTime() > GS->Now() - 1.5f ? S : Strike;
		}
		if (bCoStar && Host)
		{
			// Idle: 7 m in front of the host; a strike telegraphing: halfway between the host and the blast.
			const FVector HostAt = Host->GetActorLocation();
			const FVector Spot = Strike ? FMath::Lerp(HostAt, Strike->GetActorLocation(), 0.45f) : HostAt + Host->GetActorForwardVector().GetSafeNormal2D() * 700.f;
			if (FVector::Dist2D(Spot, Me->GetActorLocation()) > 150.f)
			{
				AimAt(FVector(Spot.X, Spot.Y, Me->GetActorLocation().Z));
				Press(TEXT("Move"), FVector(0, 1, 0));
				return;
			}
			AimAt(Strike ? Strike->GetActorLocation() : HostAt); // face the blast, in the host's frame
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
	// Frames only when asked (-dcshots=<dir>): a 1080p screenshot costs ~250 ms and would pollute perf runs (R13).
	if (FParse::Value(FCommandLine::Get(), TEXT("dcshots="), ShotDir))
	{
		IFileManager::Get().MakeDirectory(*ShotDir, true);
	}
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
	if (ShotDir.IsEmpty())
	{
		UE_LOG(LogDCDemo, Display, TEXT("DEMO step %s (no frame: -dcshots not set)"), *Name);
		return;
	}
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
	// R4 evidence: with a Destroy objective live, strike the marked towers first (any distance).
	if (const ADCObjective* O = ADCObjective::Find(this); O && O->Stage == EDCObjectiveStage::Active)
	{
		for (int32 i = 0; i < O->TargetAt.Num(); ++i)
		{
			if (O->TargetDown.IsValidIndex(i) && O->TargetDown[i])
			{
				continue;
			}
			for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
			{
				const float D = B->IsCollapsed() ? BestDist : FVector::Dist2D(B->GetActorLocation(), O->TargetAt[i]);
				Best = D < BestDist ? B : Best;
				BestDist = FMath::Min(BestDist, D);
			}
			if (Best)
			{
				return Best;
			}
		}
	}
	for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		if (B->IsCollapsed() || UsedTowers.Contains(B))
		{
			continue;
		}
		const float D = FVector::Dist2D(B->GetActorLocation(), Me->GetActorLocation());
		if (D > 4000.f && D < BestDist) // far enough that the peak frame shows the tower, not the debris at the lens
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

void ADCDemoPilot::AddClashShot(const FString& S)
{
	// R3 evidence (advisor a-9): wait for two factions fighting, frame the pair with the real camera and snap; the host
	// logs the graded photo with its tags and Factions metric (DCPHOTO graded ... tags=...CLASH).
	Add(0.f, [this]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const UDCUnits* Units = UDCUnits::Get(this);
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		if (!Me || !Units || !GS || GS->Phase != EDCRunPhase::Sector)
		{
			return !GS || GS->Phase != EDCRunPhase::Sector;
		}
		float Best = 6000.f;
		for (const ADCEnemy* U : Units->All())
		{
			const ADCEnemy* Other = U && U->IsAlive() ? Units->InClash(*U) : nullptr;
			const float D = Other ? FVector::Dist(U->GetActorLocation(), Me->GetActorLocation()) : Best;
			if (D < Best)
			{
				Best = D;
				ClashMid = (U->GetActorLocation() + Other->GetActorLocation()) * 0.5f;
			}
		}
		return Best < 6000.f;
	}, 90.f);
	Add(0.f, [this] { Tap(TEXT("Slot3")); return true; });
	Add(0.6f, [this] { Press(TEXT("Aim")); AimAt(ClashMid + FVector(0, 0, 80.f)); return true; });
	Add(0.8f, [this] { AimAt(ClashMid + FVector(0, 0, 80.f)); Shot(TEXT("clash_viewfinder")); return true; });
	Add(0.1f, [this] { Tap(TEXT("Fire")); return true; });
	Add(0.6f, [this, S] { Shot(S + TEXT("_clash_stamp")); LogPhoto(); Release(TEXT("Aim")); return true; });
}

void ADCDemoPilot::AddIndoors()
{
	// R6 evidence (-dcindoors): once the crew grace is over, the pawn is placed 3 m inside the standing tower nearest
	// to it that has a door (a logged pilot aid), holds there 30 s and logs how many hostile ground units stand inside
	// the same tower every 5 s (DEMO indoors), with frames.
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(18.f, [this]
	{
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		IndoorLot = INDEX_NONE;
		float Best = TNumericLimits<float>::Max();
		for (int32 Lot = 0; Me && City && Lot < 64; ++Lot)
		{
			FVector Door;
			if (City->NearestPortal(Lot, Me->GetActorLocation(), Door) && FVector::DistSquared2D(Door, Me->GetActorLocation()) < Best)
			{
				Best = FVector::DistSquared2D(Door, Me->GetActorLocation());
				IndoorLot = Lot;
				IndoorDoor = Door;
			}
		}
		if (Me && IndoorLot != INDEX_NONE)
		{
			const FVector Into = (IndoorDoor - Me->GetActorLocation()).GetSafeNormal2D();
			const FVector Inside = IndoorDoor + Into * 300.f + FVector(0, 0, Me->GetActorLocation().Z - IndoorDoor.Z + 20.f);
			Me->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogDCDemo, Display, TEXT("DEMO indoors: pawn placed inside lot %d at %s (door %s; pilot aid)"), IndoorLot, *Inside.ToCompactString(), *IndoorDoor.ToCompactString());
		}
		return true;
	});
	for (int32 i = 0; i < 12; ++i)
	{
		Add(5.f, [this, i]
		{
			const ADCVoxelCity* City = ADCVoxelCity::Get(this);
			const UDCUnits* Units = UDCUnits::Get(this);
			const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
			int32 Inside = 0;
			int32 Near = 0;
			const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
			for (const ADCEnemy* U : Units ? Units->All() : TArray<TObjectPtr<ADCEnemy>>())
			{
				const bool bHostile = U && U->IsAlive() && DCFactionRules::FightsCrew(Factions, U->GetFaction());
				Inside += bHostile && City && City->StandingLotAt(U->GetActorLocation()) == IndoorLot ? 1 : 0;
				Near += bHostile && Me && FVector::Dist(U->GetActorLocation(), Me->GetActorLocation()) < 1500.f ? 1 : 0;
			}
			UE_LOG(LogDCDemo, Display, TEXT("DEMO indoors t=%d s: lot %d hostiles inside=%d within 15 m=%d player inside=%d"), (i + 1) * 5, IndoorLot, Inside, Near,
				Me && City && City->StandingLotAt(Me->GetActorLocation()) == IndoorLot ? 1 : 0);
			Shot(FString::Printf(TEXT("indoors_%02d"), (i + 1) * 5));
			return true;
		});
	}
}

void ADCDemoPilot::SideCam(bool bOn)
{
	// Evidence camera: 3.5 m to the pawn's right, chest height, looking at it (the shoulder camera hides the arms).
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Me)
	{
		return;
	}
	if (!bOn)
	{
		PC->SetViewTargetWithBlend(Me, 0.f);
		return;
	}
	if (!EvidenceCam)
	{
		EvidenceCam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
	}
	const FVector Eye = Me->GetActorLocation() + Me->GetActorRightVector() * 350.f + Me->GetActorForwardVector() * 120.f + FVector(0, 0, 40.f);
	EvidenceCam->SetActorLocationAndRotation(Eye, (Me->GetActorLocation() + FVector(0, 0, 30.f) - Eye).Rotation());
	if (PC->GetViewTarget() != EvidenceCam)
	{
		PC->SetViewTargetWithBlend(EvidenceCam, 0.f);
	}
}

void ADCDemoPilot::AddAim()
{
	// R16 evidence (-dcaim): rifle out, hold the real Aim action: frames hip vs aimed (zoom, tight crosshair), aim up and
	// down from the side camera (the aim pose follows the pitch), fire while aimed; DCAIM lines with FOV and state.
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(0.6f, [this] { Tap(TEXT("Slot1")); return true; });
	Add(0.8f, [this] { Shot(TEXT("aim_hip")); LogAim(TEXT("hip")); Press(TEXT("Aim")); return true; });
	Add(0.5f, [this] { Shot(TEXT("aim_in")); LogAim(TEXT("aimed")); return true; });
	for (const float Up : {600.f, -500.f})
	{
		Add(0.4f, [this, Up]
		{
			const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
			if (Me)
			{
				AimAt(Me->GetActorLocation() + Me->GetActorForwardVector() * 1000.f + FVector(0, 0, Up));
			}
			return true;
		});
		Add(0.5f, [this, Up] { Shot(Up > 0.f ? TEXT("aim_up") : TEXT("aim_down")); LogAim(Up > 0.f ? TEXT("up") : TEXT("down")); SideCam(true); return true; });
		Add(0.3f, [this, Up] { SideCam(true); Shot(Up > 0.f ? TEXT("aim_up_side") : TEXT("aim_down_side")); return true; });
		Add(0.1f, [this] { SideCam(false); return true; });
	}
	for (int32 i = 0; i < 6; ++i)
	{
		Add(0.15f, [this, i] { Tap(TEXT("Fire")); if (i == 2) { Shot(TEXT("aim_fire")); } return true; });
	}
	Add(0.3f, [this] { Release(TEXT("Aim")); return true; });
	Add(0.5f, [this] { Shot(TEXT("aim_out")); LogAim(TEXT("released")); return true; });
}

void ADCDemoPilot::LogAim(const TCHAR* Tag)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
	if (Me && PC->PlayerCameraManager)
	{
		UE_LOG(LogDCDemo, Display, TEXT("DCAIM %s aiming=%d fov=%.1f pitch=%.1f speed=%.0f"), Tag, Me->IsRifleAiming() ? 1 : 0, PC->PlayerCameraManager->GetFOVAngle(), Me->GetAimPitch(), Me->GetCharacterMovement() ? Me->GetCharacterMovement()->MaxWalkSpeed : 0.f);
	}
}

void ADCDemoPilot::AddSwarmed()
{
	// R17 evidence (-dcswarmed): after the crew grace the pawn stands still in the street for 30 s while units close in
	// and hit it; DCSHAKE lines (hits per second vs hit shakes and flinches) and a frame every 3 s.
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(UDCTuning::Get().FactionMix.CrewGraceSeconds + 2.f, [this] { Tap(TEXT("Slot1")); return true; });
	for (int32 i = 0; i < 10; ++i)
	{
		Add(3.f, [this, i] { Shot(FString::Printf(TEXT("swarmed_%02d"), i * 3)); return true; });
	}
}

void ADCDemoPilot::AddBounds()
{
	// R19 evidence (-dcbounds): face the nearest sector edge and walk at it with the real Move/Sprint actions for 25 s
	// (DEMO bounds lines: distance to the ring every 5 s, it must stop at the ring), frame the dressed edge, then a
	// pilot aid drops the pawn 30 m under the street: the host's fall net must return it (DCKILLZ).
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(0.5f, [this]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (Me)
		{
			const FVector P = Me->GetActorLocation();
			const FVector Dir = FMath::Abs(P.X) > FMath::Abs(P.Y) ? FVector(FMath::Sign(P.X), 0, 0) : FVector(0, FMath::Sign(P.Y), 0);
			AimAt(P + Dir * 5000.f + FVector(0, 0, 60.f));
		}
		Press(TEXT("Move"), FVector(0, 1, 0));
		Press(TEXT("Sprint"));
		return true;
	});
	for (int32 i = 1; i <= 5; ++i)
	{
		Add(5.f, [this, i]
		{
			const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
			const ADCVoxelCity* City = ADCVoxelCity::Get(this);
			if (Me && City)
			{
				const FVector P = Me->GetActorLocation();
				UE_LOG(LogDCDemo, Display, TEXT("DEMO bounds t=%d s: pawn at %s, ring half %.0f, inside=%d"), i * 5, *P.ToCompactString(), City->GetBoundsHalfCm(), City->IsInsideBounds(P) ? 1 : 0);
			}
			if (i == 5)
			{
				Release(TEXT("Move"));
				Release(TEXT("Sprint"));
				Shot(TEXT("bounds_edge"));
			}
			return true;
		});
	}
	Add(0.5f, [this]
	{
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (Me)
		{
			Me->AddActorWorldRotation(FRotator(0.f, 180.f, 0.f));
			Shot(TEXT("bounds_back_to_city"));
		}
		return true;
	});
	Add(1.f, [this]
	{
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (Me && GetWorld()->GetAuthGameMode())
		{
			Me->SetActorLocation(Me->GetActorLocation() - FVector(0, 0, 3000.f), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogDCDemo, Display, TEXT("DEMO bounds: pawn dropped under the street to %s (pilot aid)"), *Me->GetActorLocation().ToCompactString());
		}
		return true;
	});
	Add(1.5f, [this]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		UE_LOG(LogDCDemo, Display, TEXT("DEMO bounds: after the drop the pawn is at %s"), Me ? *Me->GetActorLocation().ToCompactString() : TEXT("none"));
		Shot(TEXT("bounds_after_drop"));
		return true;
	});
}

void ADCDemoPilot::AddRunFire()
{
	// R10 evidence (-dcrunfire): rifle out, sprint and strafe with the real Move/Sprint actions while tapping Fire;
	// frames mid-run show the raised rifle, the tracer from the barrel and the muzzle flash.
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(0.6f, [this] { Tap(TEXT("Slot1")); return true; });
	Add(1.0f, [this] { Press(TEXT("Move"), FVector(0, 1, 0)); Press(TEXT("Sprint")); return true; });
	Add(0.f, [this] { SideCam(true); return true; });
	for (int32 i = 0; i < 14; ++i)
	{
		Add(0.11f, [this, i]
		{
			Tap(TEXT("Fire"));
			SideCam(true);
			if (i % 5 == 2)
			{
				Shot(FString::Printf(TEXT("runfire_fwd_%d"), i));
			}
			return true;
		});
	}
	Add(0.2f, [this] { Release(TEXT("Sprint")); Press(TEXT("Move"), FVector(1, 0, 0)); return true; });
	for (int32 i = 0; i < 10; ++i)
	{
		Add(0.11f, [this, i]
		{
			Tap(TEXT("Fire"));
			SideCam(true);
			if (i % 5 == 2)
			{
				Shot(FString::Printf(TEXT("runfire_strafe_%d"), i));
			}
			return true;
		});
	}
	Add(0.3f, [this] { Release(TEXT("Move")); SideCam(true); Shot(TEXT("runfire_idle")); return true; });
	Add(0.2f, [this] { SideCam(false); return true; });
}

void ADCDemoPilot::AddChipFire()
{
	// R24 evidence (-dcchipfire, with -dcchips on the host when ChipsEnabled is off in data): the pawn is placed on the
	// street ChipStandCm out from the middle of the nearest standing tower's face (a logged pilot aid), rifle out, and
	// holds the real Fire action for ChipFireSeconds at the wall's foot, sweeping a small pattern; ammo is topped up
	// (pilot aid). A fixed camera beside the pawn frames the wall before, every 10 s and after (the hole, and the
	// topple once the support is shot away); DEMO chipfire lines carry the city hash and the op count.
	constexpr float StandCm = 1100.f;
	constexpr int32 Seconds = 60;
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	Add(2.f, [this]
	{
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		if (!Me || !City)
		{
			return true;
		}
		const TArray<FDCVoxelTower>& Towers = City->GetSector().Towers;
		float Best = TNumericLimits<float>::Max();
		for (int32 Lot = 0; Lot < Towers.Num(); ++Lot)
		{
			const FVector C = FVector(Towers[Lot].FootMinCm() + Towers[Lot].FootMaxCm()) * 0.5f;
			if (!City->IsTowerFallen(Lot) && FVector::DistSquared2D(C, Me->GetActorLocation()) < Best)
			{
				Best = FVector::DistSquared2D(C, Me->GetActorLocation());
				ChipLot = Lot;
			}
		}
		if (ChipLot == INDEX_NONE)
		{
			return true;
		}
		const FDCVoxelTower& T = Towers[ChipLot];
		const FVector Lo(T.FootMinCm()), Hi(T.FootMaxCm());
		const FVector Mid = (Lo + Hi) * 0.5f;
		const FVector To = Me->GetActorLocation() - Mid;
		// The face toward the pawn, on its dominant axis.
		const FVector Out = FMath::Abs(To.X) * (Hi.Y - Lo.Y) >= FMath::Abs(To.Y) * (Hi.X - Lo.X) ? FVector(FMath::Sign(To.X), 0, 0) : FVector(0, FMath::Sign(To.Y), 0);
		const FVector Face = Mid + Out * (FMath::Abs(Out.X) > 0.f ? (Hi.X - Lo.X) : (Hi.Y - Lo.Y)) * 0.5f;
		const float Base = T.Grid.OriginCm.Z;
		ChipAim = FVector(Face.X, Face.Y, Base + 110.f);
		const FVector Stand = Face + Out * StandCm;
		const float Ground = City->SurfaceZCm(Stand);
		Me->SetActorLocation(FVector(Stand.X, Stand.Y, Ground + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		ChipEye = FVector(Stand.X, Stand.Y, Ground + 160.f) + FVector::CrossProduct(Out, FVector::UpVector) * 180.f - Out * 150.f;
		UE_LOG(LogDCDemo, Display, TEXT("DEMO chipfire: lot %d face %s, pawn placed at %s (pilot aid), camera %s"), ChipLot, *Face.ToCompactString(), *Stand.ToCompactString(), *ChipEye.ToCompactString());
		return true;
	});
	auto Frame = [this](const FString& Name)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!PC)
		{
			return;
		}
		if (!EvidenceCam)
		{
			EvidenceCam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
		}
		EvidenceCam->SetActorLocationAndRotation(ChipEye, (ChipAim + FVector(0, 0, 150.f) - ChipEye).Rotation());
		PC->SetViewTargetWithBlend(EvidenceCam, 0.f);
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		UE_LOG(LogDCDemo, Display, TEXT("DEMO chipfire %s: lot %d fallen=%d city hash=%08x ops=%d applied=%d"), *Name, ChipLot, City && City->IsTowerFallen(ChipLot) ? 1 : 0,
			City && City->IsOpQueueIdle() ? City->GetHash() : 0u, GS ? GS->VoxelOps.Num() : 0, City ? City->GetAppliedSeq() : 0); // hash 0: an op is on the worker (never hash under it)
		Shot(Name);
	};
	Add(0.6f, [this] { Tap(TEXT("Slot1")); return true; });
	Add(1.0f, [this, Frame] { Frame(TEXT("chipfire_before")); return true; });
	Add(0.3f, [this]
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
		}
		AimAt(ChipAim);
		Press(TEXT("Fire"));
		return true;
	});
	// A sweep 2 m wide and 1 m tall at the wall's foot, one aim point every half second.
	for (int32 i = 0; i < Seconds * 2; ++i)
	{
		Add(0.5f, [this, i, Frame]
		{
			ADCCharacter* Me = Cast<ADCCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
			if (Me && i % 6 == 0)
			{
				Me->HostResupply(0, 60, 0.f); // pilot aid: the magazine never runs dry over the minute
			}
			const float U = static_cast<float>(i % 8) / 7.f - 0.5f;
			const float V = static_cast<float>((i / 8) % 3) - 1.f;
			const FVector Side = FVector::CrossProduct((ChipAim - ChipEye).GetSafeNormal2D(), FVector::UpVector);
			if (i % 20 == 19)
			{
				Release(TEXT("Fire"));
				Frame(FString::Printf(TEXT("chipfire_t%02d"), (i + 1) / 2));
				return true;
			}
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			if (PC && PC->GetPawn() && PC->GetViewTarget() != PC->GetPawn())
			{
				PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
			}
			AimAt(ChipAim + Side * U * 200.f + FVector(0, 0, V * 50.f));
			Press(TEXT("Fire"));
			return true;
		});
	}
	Add(0.2f, [this] { Release(TEXT("Fire")); return true; });
	Add(3.f, [this, Frame] { Frame(TEXT("chipfire_after")); return true; });
	Add(6.f, [this, Frame] { Frame(TEXT("chipfire_settled")); return true; });
	Add(0.2f, [this]
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
		}
		return true;
	});
}

void ADCDemoPilot::AddSupplyCall()
{
	// R7 evidence (-dcsupply): spend the rifle's magazine, then Slot2 + Q until the Supply Drop is selected, call it 6 m
	// ahead with the real Fire action, frame the falling crate and walk into it (DCSUPPLY lines in the log).
	Add(0.f, [this] { const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); return GS && GS->Phase == EDCRunPhase::Sector; }, 120.f);
	for (int32 i = 0; i < 20; ++i)
	{
		Add(0.12f, [this] { Tap(TEXT("Fire")); return true; });
	}
	Add(0.5f, [this] { Shot(TEXT("supply_ammo_spent")); Tap(TEXT("Slot2")); return true; });
	for (int32 i = 0; i < 6; ++i)
	{
		Add(0.35f, [this]
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
			if (Me && Me->GetSelectedStratagem() != TEXT("SupplyDrop"))
			{
				Tap(TEXT("Cycle"));
			}
			return true;
		});
	}
	Add(0.3f, [this]
	{
		const APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (Me)
		{
			SupplyAt = Me->GetActorLocation() + Me->GetActorForwardVector() * 600.f - FVector(0, 0, 90.f);
			AimAt(SupplyAt);
		}
		Shot(TEXT("supply_aim"));
		return true;
	});
	Add(0.2f, [this] { Tap(TEXT("Fire")); UE_LOG(LogDCDemo, Display, TEXT("DEMO supply drop called at %s"), *SupplyAt.ToCompactString()); return true; });
	Add(2.0f, [this] { AimAt(SupplyAt + FVector(0, 0, 300.f)); Shot(TEXT("supply_falling")); return true; });
	Add(1.5f, [this] { AimAt(SupplyAt); Shot(TEXT("supply_landed")); Tap(TEXT("Slot1")); return true; });
	Add(0.f, [this]
	{
		// Walk to the landed pod nearest the called drop point with the real Move action until it opens (it is destroyed
		// when opened). Nearest the call point, not the pawn: a redeploy pod may stand closer (bugs4 opened that one).
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCSupplyPod* Pod = nullptr;
		for (const ADCSupplyPod* P : TActorRange<ADCSupplyPod>(GetWorld()))
		{
			const bool bCloser = !Pod || FVector::Dist2D(P->GetActorLocation(), SupplyAt) < FVector::Dist2D(Pod->GetActorLocation(), SupplyAt);
			Pod = P->HasLanded() && FVector::Dist2D(P->GetActorLocation(), SupplyAt) < 1500.f && bCloser ? P : Pod;
		}
		if (!Me || !Pod)
		{
			Release(TEXT("Move"));
			return true;
		}
		AimAt(Pod->GetActorLocation() + FVector(0, 0, 60.f));
		Press(TEXT("Move"), FVector(0, 1, 0));
		return false;
	}, 8.f);
	Add(0.6f, [this] { Release(TEXT("Move")); Shot(TEXT("supply_opened")); return true; });
}

void ADCDemoPilot::AddLineupShot()
{
	// R3/R10 readability evidence (-dclineup): every unit kind of every +Factions row (columns = factions, rows = its
	// units), stood on the street in front of the pawn, frozen (time dilation 0), framed with the real camera in the sector's own light;
	// frames lineup_wide and lineup_close, then the row is removed and the run goes on. Host (solo) only.
	Add(0.f, [this]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		return GS && GS->Phase == EDCRunPhase::Sector;
	}, 120.f);
	Add(2.f, [this]
	{
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		if (!Me || !GetWorld()->GetAuthGameMode())
		{
			return true;
		}
		const UDCTuning& T = UDCTuning::Get();
		const FVector Fwd = FVector(Me->GetActorForwardVector().X, Me->GetActorForwardVector().Y, 0.f).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
		const int32 N = T.Factions.Num();
		LineupMid = Me->GetActorLocation() + Fwd * 1100.f;
		// Columns are factions, rows their unit kinds front to back (R3 silhouettes and R10 shapes in one frame).
		for (int32 i = 0; i < N; ++i)
		{
			const TArray<FDCFactionUnit>& Units = T.Factions[i].Units;
			for (int32 r = 0; r < Units.Num(); ++r)
			{
				const FName Kind = Units[r].Enemy;
				const FDCEnemyDef* Def = T.FindEnemy(Kind);
				if (!Def)
				{
					continue;
				}
				FVector Where = LineupMid + Right * ((i - (N - 1) * 0.5f) * 320.f) + Fwd * (r * 280.f);
				Where.Z = (City ? City->SurfaceZCm(Where) : Me->GetActorLocation().Z - 90.f) + Def->HoverHeight;
				const FTransform Xf((-Fwd).Rotation(), Where);
				ADCEnemy* E = GetWorld()->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (E)
				{
					E->InitKind(Kind);
					E->FinishSpawning(Xf);
					E->CustomTimeDilation = 0.f;
					E->SetCanBeDamaged(false); // a frozen exhibit: nobody shoots it and no unit targets it
					if (UDCUnits* Live = UDCUnits::Get(this))
					{
						Live->Remove(E);
					}
					Lineup.Add(E);
					UE_LOG(LogDCDemo, Display, TEXT("DEMO lineup col=%d row=%d faction=%s unit=%s at %s"), i, r, *T.Factions[i].Id.ToString(), *Kind.ToString(), *Where.ToCompactString());
				}
			}
		}
		return true;
	});
	Add(0.5f, [this] { AimAt(LineupMid + FVector(0, 0, 60.f)); return true; });
	Add(1.5f, [this] { AimAt(LineupMid + FVector(0, 0, 60.f)); Shot(TEXT("lineup_wide")); return true; });
	Add(0.3f, [this] { Tap(TEXT("Slot3")); return true; });
	Add(0.6f, [this] { Press(TEXT("Aim")); AimAt(LineupMid + FVector(0, 0, 60.f)); return true; });
	Add(1.0f, [this] { AimAt(LineupMid + FVector(0, 0, 60.f)); Shot(TEXT("lineup_close")); Release(TEXT("Aim")); return true; });
	Add(0.3f, [this]
	{
		for (const TWeakObjectPtr<ADCEnemy>& E : Lineup)
		{
			if (E.IsValid())
			{
				E->Destroy();
			}
		}
		Lineup.Reset();
		Tap(TEXT("Slot1"));
		return true;
	});
}

void ADCDemoPilot::AddObjectiveRun(const FString& S)
{
	// R4 evidence (-dcobjrun): run the sector's objective with the real Move and Sprint actions toward the HUD's
	// waypoint (package, marked tower, beacon); frames at every stage change and every 8 s. When the street blocks the
	// straight line for 1.5 s the pawn is nudged 6 m along it (logged DEMO nudge: a pathing aid, not a player move).
	Add(0.f, [this, S]
	{
		const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
		APawn* Me = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		const ADCObjective* O = ADCObjective::Find(this);
		const double Real = GetWorld()->GetRealTimeSeconds();
		const bool bOver = !GS || GS->Phase != EDCRunPhase::Sector || !O || O->Stage == EDCObjectiveStage::Done || O->Stage == EDCObjectiveStage::Failed;
		const uint8 Stage = O ? static_cast<uint8>(O->Stage) : 0;
		if (Stage != ObjStage || Real >= ObjNextShotReal)
		{
			ObjStage = Stage;
			ObjNextShotReal = Real + 8.0;
			Shot(FString::Printf(TEXT("%s_obj_stage%d"), *S, Stage));
			UE_LOG(LogDCDemo, Display, TEXT("DEMO objective stage %d"), Stage);
		}
		FVector At;
		if (bOver || !Me || !O->Waypoint(Me->GetActorLocation(), At))
		{
			Release(TEXT("Move"));
			Release(TEXT("Sprint"));
			return bOver;
		}
		// Self-defence: a hostile unit within ObjDefendCm gets the rifle (Slot1, real Fire), like a player would.
		const ADCEnemy* Threat = nullptr;
		float ThreatD = ObjDefendCm;
		if (const UDCUnits* Live = UDCUnits::Get(this))
		{
			const TArray<FDCFactionDef>& Factions = UDCTuning::Get().Factions;
			for (const ADCEnemy* U : Live->All())
			{
				const float UD = U && U->IsAlive() && U->CanBeDamaged() && DCFactionRules::FightsCrew(Factions, U->GetFaction())
					? FVector::Dist(U->GetActorLocation(), Me->GetActorLocation()) : ThreatD;
				Threat = UD < ThreatD ? U : Threat;
				ThreatD = FMath::Min(ThreatD, UD);
			}
		}
		if (Threat && ObjStrikePhase == 0)
		{
			if (!bObjDefending)
			{
				Tap(TEXT("Slot1"));
				bObjDefending = true;
			}
			Release(TEXT("Sprint"));
			AimAt(Threat->GetActorLocation() + FVector(0, 0, 40.f));
			Tap(TEXT("Fire"));
			return false;
		}
		bObjDefending = false;
		// Destroy: within ObjStrikeRangeCm of the marked tower, with the free laser ready, call it on the tower's base with
		// the real Slot2 / Fire actions (two polls apart so the selection lands), then back to walking.
		const FDCObjectiveDef* Def = O->GetDef();
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
		const bool bStrikeReady = PS && PS->CooldownReadyTime(TEXT("OrbitalLaser")) <= GS->Now();
		// The waypoint sits on the marker above the tower: aim at the tower's lower floors (pawn height + 3 m) so the laser
		// cuts the base and the tower falls (destroy4/objcard: strikes on the marker hit sky). Inside strike range the
		// pilot holds there (never walks into the swarm at the tower's foot) until the laser is ready.
		const bool bDestroyActive = Def && Def->Kind == TEXT("Destroy") && O->Stage == EDCObjectiveStage::Active;
		const bool bInStrikeRange = FVector::Dist2D(Me->GetActorLocation(), At) < ObjStrikeRangeCm;
		const FVector Base(At.X, At.Y, Me->GetActorLocation().Z + 300.f);
		if (bDestroyActive && bInStrikeRange && !bStrikeReady && ObjStrikePhase == 0)
		{
			Release(TEXT("Move"));
			Release(TEXT("Sprint"));
			AimAt(Base);
			return false;
		}
		if (bDestroyActive && Real >= ObjStrikeReal && (ObjStrikePhase > 0 || (bStrikeReady && bInStrikeRange)))
		{
			Release(TEXT("Move"));
			Release(TEXT("Sprint"));
			AimAt(Base);
			if (ObjStrikePhase == 0)
			{
				Tap(TEXT("Slot2"));
				ObjStrikePhase = 1;
				ObjStrikeReal = Real + 0.4;
			}
			else if (ObjStrikePhase == 1)
			{
				Tap(TEXT("Fire"));
				UE_LOG(LogDCDemo, Display, TEXT("DEMO objective strike on the marked tower base at %s"), *Base.ToCompactString());
				ObjStrikePhase = 2;
				ObjStrikeReal = Real + 0.6;
			}
			else
			{
				Tap(TEXT("Slot1"));
				ObjStrikePhase = 0;
				ObjStrikeReal = Real + 1.5;
			}
			return false;
		}
		AimAt(FVector(At.X, At.Y, Me->GetActorLocation().Z + 60.f));
		Press(TEXT("Move"), FVector(0, 1, 0));
		Press(TEXT("Sprint"));
		if (Real >= ObjCheckReal)
		{
			if (FVector::Dist2D(Me->GetActorLocation(), ObjLastPos) < 150.f)
			{
				const FVector Dir = (At - Me->GetActorLocation()).GetSafeNormal2D();
				Me->SetActorLocation(Me->GetActorLocation() + Dir * 600.f + FVector(0, 0, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogDCDemo, Display, TEXT("DEMO nudge toward the waypoint"));
			}
			ObjLastPos = Me->GetActorLocation();
			ObjCheckReal = Real + 1.5;
		}
		return false;
	}, 900.f); // the step ends with the sector (bOver); finalobj-Destroy-s777-0ad4bc5 timed out at 200 s with the extract open
	Add(0.5f, [this, S] { Release(TEXT("Move")); Release(TEXT("Sprint")); Shot(S + TEXT("_obj_end")); return true; });
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
	// Defending: anything within 12 m (holding fire at 25 m emptied the rifle by sector 2). Mid-script: a 6 m guard
	// bubble, so a swarmer biting at 1-2 m is shot instead of downing the pilot while it frames a photo.
	// With the camera up (framing a swarm) it only breaks off for a swarmer already at arm's length (3 m).
	// (Sector 2 runs still went down at 1 m with a 6 m bubble: 10 m gives the rifle time to drop a rushing swarmer.)
	float Best = bDefend ? 1200.f : (Me && Me->IsCameraRaised() ? 1000.f : 1300.f);
	if (Me && !Me->IsDowned() && GS && GS->Phase == EDCRunPhase::Sector)
	{
		for (ADCEnemy* E : TActorRange<ADCEnemy>(GetWorld()))
		{
			const float D = E->IsAlive() ? FVector::Dist(E->GetActorLocation(), Me->GetActorLocation()) : Best;
			Nearest = D < Best ? E : Nearest;
			Best = FMath::Min(Best, D);
		}
	}
	// An empty magazine reloads at once; a half-empty one reloads while nothing is close (a player would).
	if (Me && !Me->IsDowned() && Me->GetSlot() == EDCSlot::Rifle && !Me->IsReloading() && Me->GetReserve() > 0
		&& (Me->GetAmmo() == 0 || (!Nearest && Me->GetAmmo() < 12)))
	{
		Tap(TEXT("Reload"));
	}
	if (Nearest)
	{
		if (Me->GetSlot() != EDCSlot::Rifle)
		{
			Tap(TEXT("Slot1"));
		}
		AimAt(Nearest->GetActorLocation());
	}
	// A swarmer inside 4 m: backpedal while firing (a player kites the bite; standing still let packs down the pilot).
	// Re-asserted every tick while a threat is near: script steps release Fire and Move on their own timers, which left
	// the cached state "firing" with the trigger up (helper CP7: the sector-2 downs).
	const bool bBack = Nearest && Best < 400.f && !Me->IsCameraRaised();
	if (bBack)
	{
		Press(TEXT("Move"), FVector(0, -1, 0));
	}
	else if (bRetreating)
	{
		Release(TEXT("Move"));
	}
	bRetreating = bBack;
	if (Nearest)
	{
		Press(TEXT("Fire"));
	}
	else if (bDefendFiring)
	{
		Release(TEXT("Fire"));
	}
	bDefendFiring = Nearest != nullptr;
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

void ADCDemoPilot::AddWeaponTour()
{
	// R8 evidence (-dcweapontour): on the gun slot, Q (the real Cycle input) switches to every weapon in turn; each is
	// drawn, fired for a moment at the tower and framed, then Q wraps back to the first. Logs DEMO weapon lines.
	for (int32 w = 1; w <= UDCTuning::Get().NumWeapons(); ++w)
	{
		Add(0.6f, [this] { Tap(TEXT("Slot1")); Tap(TEXT("Cycle")); return true; });
		if (w == UDCTuning::Get().NumWeapons())
		{
			break; // wrapped to the first weapon
		}
		Add(0.8f, [this] { if (Tower.IsValid()) { AimAt(Tower->GetActorLocation() + FVector(0, 0, 300)); } Press(TEXT("Fire")); return true; });
		Add(0.35f, [this]
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
			const FString Id = Me ? Me->ActiveWeapon().Id.ToString() : TEXT("none");
			UE_LOG(LogDCDemo, Display, TEXT("DEMO weapon index=%d id=%s ammo=%d reserve=%d"), Me ? Me->GetWeaponIndex() : -1, *Id, Me ? Me->GetAmmo() : -1, Me ? Me->GetReserve() : -1);
			Shot(TEXT("weapon_") + Id);
			return true;
		});
		Add(0.5f, [this] { Release(TEXT("Fire")); return true; });
	}
}

void ADCDemoPilot::AddStrikeReel(const FString& Ids)
{
	// R12 evidence (-dcstrikereel=A,B,..): the host calls each listed strike on the nearest tower (no unlock, no
	// points: a look check, not a play test) and the pilot frames its telegraph, peak and aftermath from where it stands.
	TArray<FString> List;
	Ids.ParseIntoArray(List, TEXT(","));
	for (const FString& Id : List)
	{
		const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(FName(*Id));
		if (!Def)
		{
			continue;
		}
		const float Telegraph = Def->TelegraphSeconds;
		const float Last = Def->BlastSpacing * FMath::Max(Def->Blasts - 1, 0);
		Add(1.f, [this, Id]
		{
			Tower = PickTower();
			ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
			const FDCStratagemDef* D = UDCTuning::Get().FindStratagem(FName(*Id));
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			if (GM && D && Tower.IsValid())
			{
				AimAt(Tower->GetActorLocation() + FVector(0, 0, 400));
				GM->CallStrike(*D, Tower->GetActorLocation(), PC);
				UE_LOG(LogDCDemo, Display, TEXT("DEMO strikereel id=%s blasts=%d pattern=%s at=%s"), *Id, D->Blasts, *D->Pattern.ToString(), *Tower->GetActorLocation().ToCompactString());
			}
			return true;
		});
		Add(FMath::Max(Telegraph - 0.6f, 0.f), [this, Id] { Shot(TEXT("strike_") + Id + TEXT("_telegraph")); return true; });
		Add(0.85f + Last, [this, Id] { Shot(TEXT("strike_") + Id + TEXT("_peak")); return true; });
		Add(2.5f, [this, Id] { Shot(TEXT("strike_") + Id + TEXT("_aftermath")); return true; });
	}
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
		if (UDCMusicSubsystem* Score = UDCMusicSubsystem::Get(this))
		{
			Score->SetDuck(0.f);
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
		if (UDCMusicSubsystem* Score = UDCMusicSubsystem::Get(this))
		{
			Score->SetDuck(1.f);
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
	if (FParse::Param(FCommandLine::Get(), TEXT("dcweapontour")))
	{
		AddWeaponTour();
	}
	FString StrikeReel;
	if (FParse::Value(FCommandLine::Get(), TEXT("dcstrikereel="), StrikeReel, false))
	{
		AddStrikeReel(StrikeReel);
	}

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
		// Run-and-fire first, on the open street at the spawn (bugs4 framed it in dark rubble after the indoors hold).
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcrunfire")))
		{
			AddRunFire();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcchipfire")))
		{
			AddChipFire();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcbounds")))
		{
			AddBounds();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcaim")))
		{
			AddAim();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcswarmed")))
		{
			AddSwarmed();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcindoors")))
		{
			AddIndoors();
		}
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dcsupply")))
		{
			AddSupplyCall();
		}
		// Before the first strike: a clean street, inside the crew grace.
		if (Sector == 0 && FParse::Param(FCommandLine::Get(), TEXT("dclineup")))
		{
			AddLineupShot();
		}
		for (int32 Cycle = 0; Cycle < Cycles; ++Cycle)
		{
			AddCycle(FString::Printf(TEXT("%s_c%d"), *S, Cycle));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("dcclashshot")))
		{
			AddClashShot(S);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("dcobjrun")))
		{
			AddObjectiveRun(S);
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
	ReelStep = Script.Num();
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
	DC_FRAME_SCOPE("Pilot");
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
	if (const ADCGameState* RunGS = GetWorld()->GetGameState<ADCGameState>(); RunGS && RunGS->bRunFailed && ReelStep != INDEX_NONE && Next < ReelStep)
	{
		// A wipe ends the sectors: skip to the reel instead of scripting sectors that will never start (CP4 review).
		UE_LOG(LogDCDemo, Display, TEXT("DEMO run wiped at step %d: on to the reel (step %d)"), Next, ReelStep);
		Next = ReelStep;
		StepStartReal = -1.0;
		StepReadyReal = Real;
		return;
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
