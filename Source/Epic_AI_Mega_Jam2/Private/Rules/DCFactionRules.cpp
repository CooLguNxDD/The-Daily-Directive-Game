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

	FDCFactionMix RollMix(const TArray<FDCFactionDef>& Factions, const FDCFactionMixRule& Rule, int32 Seed, int32 Sector, const TArray<FName>& Owners)
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
		// The sector's Owners list (data) narrows who may own the map; empty, or no listed hostile, = any hostile.
		TArray<int32> MainCandidates = Hostiles.FilterByPredicate([&Factions, &Owners](int32 i) { return Owners.Contains(Factions[i].Id); });
		const int32 MainIndex = Weighted(Factions, MainCandidates.Num() > 0 ? MainCandidates : Hostiles, &MainWeightOf, DCPcg::Unit(Seed, SaltMain, Sector));
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

	bool HasSlot(const TArray<FDCAttackSlot>& Slots, uint32 Holder, float Now, int32 Cap)
	{
		int32 Live = 0;
		for (const FDCAttackSlot& S : Slots)
		{
			if (S.Until > Now)
			{
				if (S.Holder == Holder)
				{
					return true;
				}
				++Live;
			}
		}
		return Live < Cap;
	}

	float SpendDamage(FDCDamageBucket& Bucket, float Amount, float Now, float Rate, float Burst)
	{
		if (Rate <= 0.f || Amount <= 0.f)
		{
			return FMath::Max(Amount, 0.f);
		}
		Bucket.Tokens = Bucket.Tokens < 0.f ? Burst : FMath::Min(Burst, Bucket.Tokens + FMath::Max(Now - Bucket.At, 0.f) * Rate);
		Bucket.At = Now;
		const float Landed = FMath::Min(Amount, Bucket.Tokens);
		Bucket.Tokens -= Landed;
		return Landed;
	}

	bool GrantSlot(TArray<FDCAttackSlot>& Slots, uint32 Holder, float Now, int32 Cap, float Lease)
	{
		Slots.RemoveAllSwap([Now](const FDCAttackSlot& S) { return S.Until <= Now; });
		if (FDCAttackSlot* Mine = Slots.FindByPredicate([Holder](const FDCAttackSlot& S) { return S.Holder == Holder; }))
		{
			Mine->Until = Now + Lease;
			return true;
		}
		if (Slots.Num() >= Cap)
		{
			return false;
		}
		Slots.Add({Holder, Now + Lease});
		return true;
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

	int32 ZoneOf(const FVector& At, float HalfExtent, int32 ZonesPerSide)
	{
		const int32 N = FMath::Max(1, ZonesPerSide);
		const float Size = FMath::Max(1.f, 2.f * HalfExtent / N);
		const int32 X = FMath::Clamp(FMath::FloorToInt32((At.X + HalfExtent) / Size), 0, N - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32((At.Y + HalfExtent) / Size), 0, N - 1);
		return Y * N + X;
	}

	int32 StepTerritory(TArray<FName>& Owners, const TArray<TPair<FVector, FName>>& Units, float HalfExtent, int32 ZonesPerSide, int32 MinUnits)
	{
		const int32 Zones = FMath::Max(1, ZonesPerSide) * FMath::Max(1, ZonesPerSide);
		if (Owners.Num() != Zones)
		{
			return 0;
		}
		// Counts per zone per faction, in sorted faction order so ties resolve the same on every run.
		TArray<TMap<FName, int32>> Counts;
		Counts.SetNum(Zones);
		for (const TPair<FVector, FName>& U : Units)
		{
			if (!U.Value.IsNone())
			{
				++Counts[ZoneOf(U.Key, HalfExtent, ZonesPerSide)].FindOrAdd(U.Value);
			}
		}
		int32 Flipped = 0;
		for (int32 z = 0; z < Zones; ++z)
		{
			Counts[z].KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			const int32 Held = Counts[z].FindRef(Owners[z]);
			FName Best = Owners[z];
			int32 BestN = Held;
			for (const TPair<FName, int32>& C : Counts[z])
			{
				Best = C.Value > BestN && C.Value >= MinUnits ? C.Key : Best;
				BestN = FMath::Max(BestN, C.Value >= MinUnits ? C.Value : BestN);
			}
			Flipped += Best != Owners[z] ? 1 : 0;
			Owners[z] = Best;
		}
		return Flipped;
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
