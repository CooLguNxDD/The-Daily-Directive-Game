#include "Rules/DCFactionRules.h"

#include "Rules/DCPcg.h"

namespace
{
	constexpr uint32 SaltMain = 0xFAC1u;
	constexpr uint32 SaltInvader = 0xFAC2u;

	/** Weighted pick over candidates (index into Factions) from a 0..1 roll; INDEX_NONE when all weights are 0. */
	int32 Weighted(const TArray<FDCFactionDef>& Factions, const TArray<int32>& Candidates, float (*WeightOf)(const FDCFactionDef&), float Roll)
	{
		float Total = 0.f;
		for (int32 i : Candidates)
		{
			Total += FMath::Max(0.f, WeightOf(Factions[i]));
		}
		if (Total <= 0.f)
		{
			return INDEX_NONE;
		}
		float At = FMath::Clamp(Roll, 0.f, 0.99999f) * Total;
		for (int32 i : Candidates)
		{
			const float W = FMath::Max(0.f, WeightOf(Factions[i]));
			if (W > 0.f && At < W)
			{
				return i;
			}
			At -= W;
		}
		return Candidates.Last();
	}
	float MainWeightOf(const FDCFactionDef& F) { return F.MainWeight; }
	float InvaderWeightOf(const FDCFactionDef& F) { return F.InvaderWeight; }
}

namespace DCFactionRules
{
	const FName Crew(TEXT("Crew"));

	const FDCFactionDef* Find(const TArray<FDCFactionDef>& Factions, FName Id)
	{
		return Factions.FindByPredicate([Id](const FDCFactionDef& F) { return F.Id == Id; });
	}

	bool IsHostile(const TArray<FDCFactionDef>& Factions, FName A, FName B)
	{
		if (A == B || A.IsNone() || B.IsNone())
		{
			return false;
		}
		const FDCFactionDef* Fa = Find(Factions, A);
		const FDCFactionDef* Fb = Find(Factions, B);
		return (Fa && Fa->Hostile.Contains(B)) || (Fb && Fb->Hostile.Contains(A));
	}

	bool FightsCrew(const TArray<FDCFactionDef>& Factions, FName Id)
	{
		return IsHostile(Factions, Id, Crew);
	}

	FDCFactionMix RollMix(const TArray<FDCFactionDef>& Factions, const FDCFactionMixRule& Rule, int32 Seed, int32 Sector)
	{
		FDCFactionMix Mix;
		TArray<int32> Hostiles;
		for (int32 i = 0; i < Factions.Num(); ++i)
		{
			if (FightsCrew(Factions, Factions[i].Id))
			{
				Hostiles.Add(i);
			}
			else if (Factions[i].InvaderWeight > 0.f || Factions[i].MainWeight > 0.f)
			{
				Mix.Allies.Add(Factions[i].Id);
			}
		}
		const int32 MainIndex = Weighted(Factions, Hostiles, &MainWeightOf, DCPcg::Unit(Seed, SaltMain, Sector));
		if (MainIndex == INDEX_NONE)
		{
			return Mix;
		}
		Mix.Main = Factions[MainIndex].Id;
		Hostiles.Remove(MainIndex);
		// Invaders: weighted picks without replacement, each roll its own hash index.
		float InvaderTotal = 0.f;
		for (int32 n = 0; n < Rule.Invaders && Hostiles.Num() > 0; ++n)
		{
			const int32 Pick = Weighted(Factions, Hostiles, &InvaderWeightOf, DCPcg::Unit(Seed, SaltInvader, Sector * 16 + n));
			if (Pick == INDEX_NONE)
			{
				break;
			}
			Mix.Invaders.Add(Factions[Pick].Id);
			Mix.InvaderShares.Add(Factions[Pick].InvaderWeight);
			InvaderTotal += Factions[Pick].InvaderWeight;
			Hostiles.Remove(Pick);
		}
		Mix.MainShare = Mix.Invaders.Num() > 0 ? FMath::Clamp(Rule.MainShare, 0.f, 1.f) : 1.f;
		for (float& S : Mix.InvaderShares)
		{
			S = InvaderTotal > 0.f ? S / InvaderTotal * (1.f - Mix.MainShare) : 0.f;
		}
		return Mix;
	}

	FName PickDropFaction(const FDCFactionMix& Mix, float Roll)
	{
		float At = FMath::Clamp(Roll, 0.f, 0.99999f);
		if (At < Mix.MainShare || Mix.Invaders.Num() == 0)
		{
			return Mix.Main;
		}
		At -= Mix.MainShare;
		for (int32 i = 0; i < Mix.Invaders.Num(); ++i)
		{
			if (At < Mix.InvaderShares[i])
			{
				return Mix.Invaders[i];
			}
			At -= Mix.InvaderShares[i];
		}
		return Mix.Invaders.Last();
	}

	FName PickUnit(const FDCFactionDef& Faction, float Roll)
	{
		float Total = 0.f;
		for (const FDCFactionUnit& U : Faction.Units)
		{
			Total += FMath::Max(0.f, U.Weight);
		}
		if (Total <= 0.f)
		{
			return NAME_None;
		}
		float At = FMath::Clamp(Roll, 0.f, 0.99999f) * Total;
		for (const FDCFactionUnit& U : Faction.Units)
		{
			const float W = FMath::Max(0.f, U.Weight);
			if (W > 0.f && At < W)
			{
				return U.Enemy;
			}
			At -= W;
		}
		return Faction.Units.Last().Enemy;
	}
}
