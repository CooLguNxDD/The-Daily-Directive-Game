#include "Rules/DCWeather.h"

namespace DCWeather
{
	const FDCSeasonDef* SeasonFor(const TArray<FDCSeasonDef>& Seasons, const FDCPcgRuleSet* Rules, int32 SectorSeed)
	{
		if (Seasons.Num() == 0)
		{
			return nullptr;
		}
		const FDCPcgRule* Rule = Rules ? Rules->Find(TEXT("Season")) : nullptr;
		const FName Id = Rule ? DCPcg::Pick(*Rule, SectorSeed, 0) : NAME_None;
		const FDCSeasonDef* Found = Seasons.FindByPredicate([Id](const FDCSeasonDef& S) { return S.Id == Id; });
		return Found ? Found : &Seasons[0];
	}

	const FDCWeatherArc* ArcFor(const FDCSeasonDef& Season, int32 SectorSeed)
	{
		if (Season.Arcs.Num() == 0)
		{
			return nullptr;
		}
		FDCPcgRule Rule;
		Rule.Slot = TEXT("Arc");
		for (const FDCWeatherArc& A : Season.Arcs)
		{
			Rule.Choices.Add({A.Id, A.Weight});
		}
		const FName Id = DCPcg::Pick(Rule, SectorSeed, 1);
		const FDCWeatherArc* Found = Season.Arcs.FindByPredicate([Id](const FDCWeatherArc& A) { return A.Id == Id; });
		return Found ? Found : &Season.Arcs[0];
	}

	FDCWeatherProfile Lerp(const FDCWeatherProfile& A, const FDCWeatherProfile& B, float T)
	{
		FDCWeatherProfile R = T < 0.5f ? A : B; // names and the precipitation kind switch at the midpoint
		R.SunScale = FMath::Lerp(A.SunScale, B.SunScale, T);
		R.SunTint = FMath::Lerp(A.SunTint, B.SunTint, T);
		R.SkyScale = FMath::Lerp(A.SkyScale, B.SkyScale, T);
		R.FogDensity = FMath::Lerp(A.FogDensity, B.FogDensity, T);
		R.FogColor = FMath::Lerp(A.FogColor, B.FogColor, T);
		R.Wind = FMath::Lerp(A.Wind, B.Wind, T);
		R.CloudCover = FMath::Lerp(A.CloudCover, B.CloudCover, T);
		R.CloudShade = FMath::Lerp(A.CloudShade, B.CloudShade, T);
		R.Wetness = FMath::Lerp(A.Wetness, B.Wetness, T);
		// Precipitation fades out then in, never pops: the rate dips through the switch.
		const float RateA = A.PrecipitationRate * FMath::Clamp(1.f - 2.f * T, 0.f, 1.f);
		const float RateB = B.PrecipitationRate * FMath::Clamp(2.f * T - 1.f, 0.f, 1.f);
		R.PrecipitationRate = T < 0.5f ? RateA : RateB;
		R.Exposure = FMath::Lerp(A.Exposure, B.Exposure, T);
		R.Saturation = FMath::Lerp(A.Saturation, B.Saturation, T);
		R.Contrast = FMath::Lerp(A.Contrast, B.Contrast, T);
		R.Gain = FMath::Lerp(A.Gain, B.Gain, T);
		return R;
	}

	FDCWeatherState At(const TArray<FDCWeatherProfile>& Profiles, const FDCSeasonDef& Season, const FDCWeatherArc& Arc, float Fraction)
	{
		FDCWeatherState S;
		S.Season = Season.Id;
		S.Arc = Arc.Id;
		auto Find = [&Profiles](FName Id) -> const FDCWeatherProfile*
		{
			return Profiles.FindByPredicate([Id](const FDCWeatherProfile& P) { return P.Id == Id; });
		};
		if (Arc.Keys.Num() == 0)
		{
			return S;
		}
		const float F = FMath::Clamp(Fraction, 0.f, 1.f);
		int32 Next = Arc.Keys.IndexOfByPredicate([F](const FDCWeatherKey& K) { return K.At > F; });
		Next = Next == INDEX_NONE ? Arc.Keys.Num() - 1 : Next;
		const int32 Prev = FMath::Max(0, Next - 1);
		const FDCWeatherKey& A = Arc.Keys[Next == 0 ? 0 : Prev];
		const FDCWeatherKey& B = Arc.Keys[Next];
		const float Span = FMath::Max(B.At - A.At, 0.0001f);
		const float T = &A == &B ? 0.f : FMath::SmoothStep(0.f, 1.f, FMath::Clamp((F - A.At) / Span, 0.f, 1.f));
		const FDCWeatherProfile* Pa = Find(A.Profile);
		const FDCWeatherProfile* Pb = Find(B.Profile);
		if (!Pa || !Pb)
		{
			return S;
		}
		S.From = A.Profile;
		S.To = B.Profile;
		S.Alpha = T;
		S.Blend = Lerp(*Pa, *Pb, T);
		return S;
	}
}
