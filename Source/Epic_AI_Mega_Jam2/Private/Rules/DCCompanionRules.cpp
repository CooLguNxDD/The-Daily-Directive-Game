#include "Rules/DCCompanionRules.h"

namespace
{
	using FMoverFn = FVector (*)(const FDCCompanionDef&, const FVector&, float, int32, int32);

	FVector Bob(const FDCCompanionDef& Def, float Now, int32 Slot)
	{
		return FVector(0.f, 0.f, Def.HeightCm + Def.BobCm * FMath::Sin(UE_TWO_PI * Def.BobHz * Now + Slot * 1.7f));
	}

	/** Circles the anchor at OrbitRadiusCm, one lap per OrbitSeconds, slots spread evenly. */
	FVector Orbit(const FDCCompanionDef& Def, const FVector& Anchor, float Now, int32 Slot, int32 Slots)
	{
		const float Lap = Def.OrbitSeconds > 0.f ? Now / Def.OrbitSeconds : 0.f;
		const float Angle = UE_TWO_PI * (Lap + static_cast<float>(Slot) / FMath::Max(Slots, 1));
		return Anchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Def.OrbitRadiusCm + Bob(Def, Now, Slot);
	}

	/** Stays where it was set down, its gun at HeightCm (no bob: a tripod does not float). */
	FVector Fixed(const FDCCompanionDef& Def, const FVector& Anchor, float, int32, int32)
	{
		return Anchor + FVector(0.f, 0.f, Def.HeightCm);
	}

	const TMap<FName, FMoverFn>& Movers()
	{
		static const TMap<FName, FMoverFn> Map = {
			{TEXT("Orbit"), &Orbit},
			{TEXT("Fixed"), &Fixed},
		};
		return Map;
	}
}

namespace DCCompanionRules
{
	FVector Position(const FDCCompanionDef& Def, const FVector& Anchor, float Now, int32 Slot, int32 Slots)
	{
		const FMoverFn* Fn = Movers().Find(Def.Mover);
		return Fn ? (*Fn)(Def, Anchor, Now, Slot, Slots) : Anchor;
	}

	bool IsKnownMover(FName Mover)
	{
		return Movers().Contains(Mover);
	}

	int32 PickTarget(const FVector& From, float RangeCm, const TArray<FVector>& Candidates)
	{
		int32 Best = INDEX_NONE;
		float BestSq = FMath::Square(RangeCm);
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			const float Sq = FVector::DistSquared(From, Candidates[i]);
			Best = Sq <= BestSq ? i : Best;
			BestSq = FMath::Min(BestSq, Sq);
		}
		return Best;
	}

	FVector PlaceAim(const FDCCompanionDef& Def, const FVector& Feet, const FVector& Forward)
	{
		const FVector Flat = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
		return Feet + (Flat.IsNearlyZero() ? FVector::ForwardVector : Flat) * Def.PlaceDistanceCm;
	}

	bool PlaceOk(const FDCCompanionDef& Def, float FeetZ, float GroundZ)
	{
		return FMath::Abs(GroundZ - FeetZ) <= Def.MaxStepCm;
	}
}
