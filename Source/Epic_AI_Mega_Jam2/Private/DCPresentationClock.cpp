#include "DCPresentationClock.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

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
	UWorld* W = GetWorld();
	const double Real = W->GetRealTimeSeconds();
	const float RealDt = LastReal < 0.0 ? 0.f : static_cast<float>(Real - LastReal);
	LastReal = Real;
	if (Real >= HoldUntilReal && Current < 1.f)
	{
		Current = FMath::Min(1.f, Current + (1.f - Target) * RealDt / Ramp); // back to live over the ramp
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
