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

		Grade.RawPoints = FMath::RoundToInt(Raw);
		Grade.Score = FMath::Max(0, FMath::RoundToInt(Raw * Grade.Multiplier));
		return Grade;
	}
}
