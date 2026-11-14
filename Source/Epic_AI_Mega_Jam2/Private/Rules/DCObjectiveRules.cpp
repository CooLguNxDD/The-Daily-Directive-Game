#include "Rules/DCObjectiveRules.h"

#include "Rules/DCPcg.h"

namespace
{
	constexpr uint32 SaltObjective = 0x0B1Eu;
	constexpr uint32 SaltTargets = 0x0B1Fu;

	/** Per kind: when the work is done (Active -> Extract) and when the crew is out (Extract -> Done). */
	struct FKindRule
	{
		bool (*WorkDone)(const FDCObjectiveInputs&) = nullptr;
		bool (*Out)(const FDCObjectiveInputs&) = nullptr;
	};
	const TMap<FName, FKindRule>& Kinds()
	{
		static const TMap<FName, FKindRule> Map = {
			{TEXT("Retrieve"), {[](const FDCObjectiveInputs& In) { return In.bCarried; }, [](const FDCObjectiveInputs& In) { return In.bCarried && In.bCarrierInZone; }}},
			{TEXT("Destroy"), {[](const FDCObjectiveInputs& In) { return In.Targets > 0 && In.Destroyed >= In.Targets; }, [](const FDCObjectiveInputs& In) { return In.bAnyoneInZone; }}},
		};
		return Map;
	}
}

namespace DCObjectiveRules
{
	const FDCObjectiveDef* Pick(const TArray<FDCObjectiveDef>& Defs, int32 Seed, int32 Sector)
	{
		float Total = 0.f;
		for (const FDCObjectiveDef& D : Defs)
		{
			Total += FMath::Max(0.f, D.Weight);
		}
		if (Total <= 0.f)
		{
			return nullptr;
		}
		float At = FMath::Clamp(DCPcg::Unit(Seed, SaltObjective, Sector), 0.f, 0.99999f) * Total;
		for (const FDCObjectiveDef& D : Defs)
		{
			const float W = FMath::Max(0.f, D.Weight);
			if (W > 0.f && At < W)
			{
				return &D;
			}
			At -= W;
		}
		return &Defs.Last();
	}

	EDCObjectiveStage Next(FName Kind, EDCObjectiveStage Stage, const FDCObjectiveInputs& In)
	{
		const FKindRule* Rule = Kinds().Find(Kind);
		const bool bLive = Stage == EDCObjectiveStage::Active || Stage == EDCObjectiveStage::Extract;
		if (!Rule || !bLive)
		{
			return Stage;
		}
		if (Stage == EDCObjectiveStage::Extract && Rule->Out(In))
		{
			return EDCObjectiveStage::Done;
		}
		if (In.bTimeUp)
		{
			return EDCObjectiveStage::Failed;
		}
		// A dropped package closes nothing: the beacon stays lit once the work was done (a carrier can go down).
		return Stage == EDCObjectiveStage::Active && Rule->WorkDone(In) ? EDCObjectiveStage::Extract : Stage;
	}

	TArray<int32> PickTargets(const TArray<FVector>& Candidates, const FVector& From, int32 Count, float MinApartCm, int32 Seed)
	{
		// Order by distance from From plus a seeded jitter (so seeds vary), then take greedily with spacing; relax the
		// spacing until enough are picked.
		TArray<int32> Order;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			Order.Add(i);
		}
		auto Score = [&](int32 i) { return FVector::Dist2D(Candidates[i], From) * (0.6f + 0.8f * DCPcg::Unit(Seed, SaltTargets, i)); };
		Order.Sort([&](int32 A, int32 B) { const float Sa = Score(A), Sb = Score(B); return Sa != Sb ? Sa > Sb : A < B; });
		TArray<int32> Out;
		for (float Apart = MinApartCm; Out.Num() < Count && Apart >= 0.f; Apart = Apart > 1.f ? Apart * 0.5f : -1.f)
		{
			for (int32 i : Order)
			{
				const bool bClear = !Out.Contains(i) && !Out.ContainsByPredicate([&](int32 j) { return FVector::Dist2D(Candidates[i], Candidates[j]) < Apart; });
				if (bClear && Out.Num() < Count)
				{
					Out.Add(i);
				}
			}
		}
		return Out;
	}

	int32 Farthest(const TArray<FVector>& Points, const FVector& From)
	{
		int32 Best = INDEX_NONE;
		float BestD = -1.f;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const float D = FVector::DistSquared2D(Points[i], From);
			Best = D > BestD ? i : Best;
			BestD = FMath::Max(BestD, D);
		}
		return Best;
	}

	int32 AtLeast(const TArray<FVector>& Points, const FVector& From, float MinDistance)
	{
		int32 Best = INDEX_NONE;
		float BestGap = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const float D = FVector::Dist2D(Points[i], From);
			// Beyond the minimum, the nearest such point; short of it, a big penalty (used only when none qualifies).
			const float Gap = D >= MinDistance ? D - MinDistance : (MinDistance - D) * 100.f;
			Best = Gap < BestGap ? i : Best;
			BestGap = FMath::Min(BestGap, Gap);
		}
		return Best;
	}
}
