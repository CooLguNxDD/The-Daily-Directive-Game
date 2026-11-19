#include "Rules/DCPhotoRules.h"

namespace DCPhotoRules
{
	const FDCTimingWindow* FindWindow(const FDCPhotoTuning& Tuning, float SecondsSinceBlast)
	{
		return Tuning.TimingWindows.FindByPredicate([SecondsSinceBlast](const FDCTimingWindow& W)
		{
			return SecondsSinceBlast >= W.MinSeconds && SecondsSinceBlast <= W.MaxSeconds;
		});
	}

	static bool CategoryEarned(const FDCCategoryRule& Rule, float Value)
	{
		return Rule.bAtMost ? Value <= Rule.Threshold : Value >= Rule.Threshold;
	}

	float CoStarMultiplier(float Fill, const FDCPhotoTuning& T, bool bDowned)
	{
		if (Fill < T.CoStarMinFill)
		{
			return 1.f;
		}
		const float U = FMath::Clamp((Fill - T.CoStarMinFill) / FMath::Max(T.CoStarFullFill - T.CoStarMinFill, 0.001f), 0.f, 1.f);
		const float Living = FMath::Lerp(1.f + (T.CoStarMaxMultiplier - 1.f) * 0.25f, T.CoStarMaxMultiplier, U);
		return bDowned ? 1.f + (Living - 1.f) * FMath::Clamp(T.CoStarDownedShare, 0.f, 1.f) : Living;
	}

	FDCPhotoGrade GradePhoto(const FDCPhotoInputs& In, const FDCPhotoTuning& Tuning)
	{
		FDCPhotoGrade Grade;
		float Raw = static_cast<float>(Tuning.BasePoints);
		Raw += Tuning.FramingPoints * FMath::Clamp(In.Centering, 0.f, 1.f) * FMath::Clamp(In.Visibility, 0.f, 1.f);

		const float Falloff = FMath::Max(Tuning.DangerFalloffMeters, 1.f);
		Raw += Tuning.DangerPoints * In.DangerScale * FMath::Clamp(1.f - In.DangerMeters / Falloff, 0.f, 1.f);

		for (const FDCCategoryRule& Rule : Tuning.Categories)
		{
			const float* Value = In.Metrics.Find(Rule.Metric);
			if (!Value || !CategoryEarned(Rule, *Value))
			{
				continue;
			}
			const float Units = FMath::Clamp(*Value, 0.f, Rule.MaxUnits);
			Raw += Rule.BasePoints + Rule.PointsPerUnit * Units;
			Grade.Categories.AddUnique(Rule.Tag);
		}

		const FDCTimingWindow* Window = In.bHasBlast ? FindWindow(Tuning, In.SecondsSinceBlast) : nullptr;
		Grade.TimingLabel = Window ? Window->Label : Tuning.NoTimingLabel;
		Grade.Multiplier = Window ? Window->Multiplier : 1.f;

		Grade.bSpotlit = In.bSpotlit;
		const float SpotlitScale = In.bSpotlit ? Tuning.SpotlitMultiplier : 1.f;
		Grade.Multiplier *= SpotlitScale * FMath::Max(In.ExtraMultiplier, 0.f);
		if (In.bSpotlit)
		{
			Grade.Categories.Insert(Tuning.SpotlitLabel, 0);
		}

		Grade.CoStarMultiplier = CoStarMultiplier(In.CoStarFill, Tuning, In.bCoStarDowned);
		if (Grade.CoStarMultiplier > 1.f)
		{
			Grade.Multiplier *= Grade.CoStarMultiplier;
			Grade.Categories.AddUnique(Tuning.CoStarTag);
		}

		if (In.bLastFrame)
		{
			Grade.bLastFrame = true;
			Grade.Multiplier *= Tuning.LastFrameMultiplier;
			Grade.Categories.Insert(Tuning.LastFrameTag, 0);
		}

		Grade.RawPoints = FMath::RoundToInt(Raw);
		Grade.Score = FMath::Max(0, FMath::RoundToInt(Raw * Grade.Multiplier));
		if (In.bLastFrame)
		{
			// It can match the best living shot of the sector, never beat it: dying is never the best strategy.
			const int32 Cap = In.LastFrameCap > 0 ? In.LastFrameCap : Grade.RawPoints;
			Grade.Score = FMath::Min(Grade.Score, Cap);
		}
		return Grade;
	}
}
