#include "DCPresentationClock.h"
#include "DCFrameScopes.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "DCEnemy.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Catch-up speed after the hold (presentation seconds per real second on top of the dilation). */
	constexpr float CatchUpRate = 1.5f;
	/** History kept per actor (s): longer than any hold plus catch-up. */
	constexpr double HistorySeconds = 8.0;

	FTransform Sample(const TArray<TPair<double, FTransform>>& H, double T)
	{
		if (H.Num() == 0)
		{
			return FTransform::Identity;
		}
		if (T <= H[0].Key)
		{
			return H[0].Value;
		}
		for (int32 i = 1; i < H.Num(); ++i)
		{
			if (H[i].Key >= T)
			{
				const double Span = FMath::Max(H[i].Key - H[i - 1].Key, 1e-4);
				const float A = static_cast<float>((T - H[i - 1].Key) / Span);
				FTransform Out;
				Out.Blend(H[i - 1].Value, H[i].Value, A);
				return Out;
			}
		}
		return H.Last().Value;
	}
}

void FDCPresentationRestoreTick::ExecuteTick(float, ELevelTick, ENamedThreads::Type, const FGraphEventRef&)
{
	if (Owner)
	{
		Owner->RestoreVisuals();
	}
}

void UDCPresentationClock::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	PresentTime = InWorld.GetTimeSeconds();
	RestoreTick.Owner = this;
	RestoreTick.TickGroup = TG_PrePhysics;
	RestoreTick.bCanEverTick = true;
	RestoreTick.bTickEvenWhenPaused = true;
	if (InWorld.PersistentLevel)
	{
		RestoreTick.RegisterTickFunction(InWorld.PersistentLevel);
	}
}

void UDCPresentationClock::Deinitialize()
{
	RestoreVisuals();
	if (RestoreTick.IsTickFunctionRegistered())
	{
		RestoreTick.UnRegisterTickFunction();
	}
	Super::Deinitialize();
}

void UDCPresentationClock::RestoreVisuals()
{
	for (const TPair<TWeakObjectPtr<USceneComponent>, FTransform>& R : RealRelative)
	{
		if (USceneComponent* C = R.Key.Get())
		{
			C->SetRelativeTransform(R.Value);
		}
	}
	RealRelative.Reset();
}

void UDCPresentationClock::TrackAndLag(UWorld* W, double Now)
{
	const APlayerController* PC = W->GetFirstPlayerController();
	const APawn* Own = PC ? PC->GetPawn() : nullptr;
	auto Track = [this, Now, Own](AActor* A)
	{
		if (!A || A == Own || !A->GetRootComponent())
		{
			return;
		}
		TArray<TPair<double, FTransform>>& H = History.FindOrAdd(A);
		H.Add({Now, A->GetActorTransform()});
		while (H.Num() > 2 && H[0].Key < Now - HistorySeconds)
		{
			H.RemoveAt(0);
		}
		if (PresentTime >= Now - 1e-3)
		{
			return; // caught up: drawn live
		}
		// Visual children ride the lagged root: relative' = relative * lagged * real^-1 (collision root stays live).
		const FTransform Lagged = Sample(H, PresentTime);
		const FTransform Real = A->GetActorTransform();
		for (USceneComponent* Child : A->GetRootComponent()->GetAttachChildren())
		{
			if (!Child)
			{
				continue;
			}
			const FTransform Rel = Child->GetRelativeTransform();
			RealRelative.Add(Child, Rel);
			Child->SetRelativeTransform(Rel * Lagged * Real.Inverse());
		}
	};
	for (TActorIterator<ADCEnemy> It(W); It; ++It)
	{
		Track(*It);
	}
	for (TActorIterator<ACharacter> It(W); It; ++It)
	{
		Track(*It);
	}
}

float UDCPresentationClock::Dilation(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UDCPresentationClock* Clock = W ? W->GetSubsystem<UDCPresentationClock>() : nullptr;
	return Clock ? Clock->Current : 1.f;
}

void UDCPresentationClock::Begin(float InDilation, float RealSeconds, float RampSeconds)
{
	Target = FMath::Clamp(InDilation, 0.05f, 1.f);
	Current = Target;
	Ramp = FMath::Max(RampSeconds, 0.01f);
	HoldUntilReal = GetWorld()->GetRealTimeSeconds() + RealSeconds;
	UE_LOG(LogTemp, Display, TEXT("DCLASTFRAME presentation slow motion %.2f for %.1f s (this machine only, simulation at real time)"), Target, RealSeconds);
}

void UDCPresentationClock::Tick(float DeltaTime)
{
	DC_FRAME_SCOPE("PresentClock");
	UWorld* W = GetWorld();
	const double Real = W->GetRealTimeSeconds();
	const float RealDt = LastReal < 0.0 ? 0.f : static_cast<float>(Real - LastReal);
	LastReal = Real;
	if (Real >= HoldUntilReal && Current < 1.f)
	{
		Current = FMath::Min(1.f, Current + (1.f - Target) * RealDt / Ramp); // back to live over the ramp
	}
	// Moving actors follow the clock too: drawn at PresentTime from their recorded path, then caught up.
	const double Now = W->GetTimeSeconds();
	if (Current < 1.f || bLagging)
	{
		if (!bLagging)
		{
			bLagging = true;
			PresentTime = Now;
			History.Reset();
		}
		const float WorldDt = DeltaTime;
		PresentTime += WorldDt * (Current < 1.f ? Current : Current + CatchUpRate);
		PresentTime = FMath::Min(PresentTime, Now);
		TrackAndLag(W, Now);
		if (Current >= 1.f && PresentTime >= Now - 1e-3)
		{
			bLagging = false;
			History.Reset();
			UE_LOG(LogTemp, Display, TEXT("DCLASTFRAME presentation caught up: moving actors live again"));
		}
	}
	else
	{
		PresentTime = Now;
	}
	// Animation on this machine follows the clock (every character it draws); restored once at 1.
	if (!FMath::IsNearlyEqual(Current, AppliedAnim))
	{
		for (TActorIterator<ACharacter> It(W); It; ++It)
		{
			if (USkeletalMeshComponent* Mesh = It->GetMesh())
			{
				Mesh->GlobalAnimRateScale = Current;
			}
		}
		AppliedAnim = Current;
	}
}
