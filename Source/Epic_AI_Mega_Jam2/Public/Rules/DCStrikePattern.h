// R12 strike blast layouts: a registry of pure functions (no engine state), keyed by the stratagem row's Pattern.
// CallStrike places each blast of a multi-blast strike at Offset(...) from the target; the tests read Known().
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

namespace DCStrikePattern
{
	struct FLayout
	{
		int32 Index = 0;
		int32 Count = 1;
		float SpanCm = 0.f;
		/** Unit direction of the caller's line of fire (ground plane). */
		FVector2D Along = FVector2D(1.f, 0.f);
	};

	using FPatternFn = FVector2D (*)(const FLayout&, FRandomStream&);

	inline const TMap<FName, FPatternFn>& Registry()
	{
		static const TMap<FName, FPatternFn> Map = {
			// Random within the span (the first blast on the target); the original barrage.
			{TEXT("Scatter"), [](const FLayout& L, FRandomStream& R) {
				return L.Index == 0 ? FVector2D::ZeroVector : FVector2D(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)).GetSafeNormal() * R.FRandRange(0.3f, 1.f) * L.SpanCm; }},
			// Walked along the line of fire, centred on the target (a strafing pass).
			{TEXT("Line"), [](const FLayout& L, FRandomStream&) {
				const float U = L.Count > 1 ? static_cast<float>(L.Index) / (L.Count - 1) - 0.5f : 0.f;
				return L.Along * U * L.SpanCm; }},
			// The first on the target, the rest evenly on a ring of the span.
			{TEXT("Ring"), [](const FLayout& L, FRandomStream&) {
				const float Angle = L.Count > 1 ? UE_TWO_PI * (L.Index - 1) / (L.Count - 1) : 0.f;
				return L.Index == 0 ? FVector2D::ZeroVector : FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * L.SpanCm; }},
		};
		return Map;
	}

	inline bool Known(FName Pattern) { return Registry().Contains(Pattern); }

	/** Offset of one blast from the target; an unknown pattern scatters. */
	inline FVector2D Offset(FName Pattern, const FLayout& Layout, FRandomStream& Rng)
	{
		const FPatternFn* Fn = Registry().Find(Pattern);
		return (Fn ? *Fn : *Registry().Find(TEXT("Scatter")))(Layout, Rng);
	}
}
