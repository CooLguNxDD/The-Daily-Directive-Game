#include "Rules/DCRunRules.h"

#include "DCTuning.h"

namespace DCRunRules
{
	TArray<FName> DraftOffer(int32 Seed, const TArray<FDCUpgradeDef>& Upgrades, const TArray<FName>& Taken, int32 PerPool)
	{
		TArray<FName> Pools;
		for (const FDCUpgradeDef& U : Upgrades)
		{
			Pools.AddUnique(U.Pool);
		}
		FRandomStream Rng(Seed);
		TArray<FName> Offer;
		for (const FName& Pool : Pools)
		{
			TArray<FName> Open;
			for (const FDCUpgradeDef& U : Upgrades)
			{
				if (U.Pool == Pool && !Taken.Contains(U.Id))
				{
					Open.Add(U.Id);
				}
			}
			while (Open.Num() > PerPool)
			{
				Open.RemoveAt(Rng.RandRange(0, Open.Num() - 1));
			}
			Offer.Append(Open);
		}
		return Offer;
	}

	bool ScoopMet(const FDCScoopDef& Scoop, const FDCPhotoInputs& In, const FDCPhotoGrade& Grade)
	{
		if (!Scoop.Timing.IsNone() && Grade.TimingLabel != Scoop.Timing)
		{
			return false;
		}
		if (Scoop.Metric.IsNone())
		{
			return true;
		}
		const float* Value = In.Metrics.Find(Scoop.Metric);
		if (!Value)
		{
			return false;
		}
		return Scoop.bAtMost ? *Value <= Scoop.Threshold : *Value >= Scoop.Threshold;
	}

	int32 PickScoop(FRandomStream& Rng, int32 NumScoops, int32 Last)
	{
		if (NumScoops <= 0)
		{
			return -1;
		}
		if (NumScoops == 1)
		{
			return 0;
		}
		const int32 Pick = Rng.RandRange(0, NumScoops - 2);
		return Pick >= Last && Last >= 0 ? Pick + 1 : Pick;
	}

	TArray<int32> CriticOrder(const TArray<FDCPhotoRecord>& Photos)
	{
		TArray<int32> Order;
		for (int32 i = 0; i < Photos.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.StableSort([&Photos](int32 A, int32 B) { return Photos[A].Grade.Score > Photos[B].Grade.Score; });
		return Order;
	}

	void InsertByCritic(TArray<int32>& Choices, const TArray<FDCPhotoRecord>& Photos, int32 NewId)
	{
		auto ScoreOf = [&Photos](int32 Id)
		{
			const FDCPhotoRecord* P = Photos.FindByPredicate([Id](const FDCPhotoRecord& R) { return R.PhotoId == Id; });
			return P ? P->Grade.Score : 0;
		};
		if (Choices.Contains(NewId))
		{
			return;
		}
		const int32 NewScore = ScoreOf(NewId);
		int32 At = 0;
		while (At < Choices.Num() && ScoreOf(Choices[At]) >= NewScore)
		{
			++At; // an earlier shot keeps its place on a tie
		}
		Choices.Insert(NewId, At);
	}

	int32 PageScore(int32 CriticScore, int32 Votes, int32 VoteBonus)
	{
		return CriticScore + Votes * VoteBonus;
	}

	int32 HeadlineIndex(const TArray<FDCPagePick>& Picks)
	{
		int32 Best = -1;
		for (int32 i = 0; i < Picks.Num(); ++i)
		{
			Best = (Best < 0 || Picks[i].PageScore > Picks[Best].PageScore) ? i : Best;
		}
		return Best;
	}

	FString MakeHeadline(int32 Seed, const TArray<FString>& Templates, const TMap<FName, FString>& Subjects,
		const FDCPhotoGrade& Grade, const FString& SectorName, const FString& Player)
	{
		if (Templates.Num() == 0)
		{
			return FString::Printf(TEXT("VICTORY IN %s"), *SectorName.ToUpper());
		}
		FRandomStream Rng(Seed);
		FString Subject = TEXT("THE ENEMY");
		for (const FName& Category : Grade.Categories)
		{
			if (const FString* Found = Subjects.Find(Category))
			{
				Subject = *Found;
				break;
			}
		}
		FString Line = Templates[Rng.RandRange(0, Templates.Num() - 1)];
		Line.ReplaceInline(TEXT("{SUBJECT}"), *Subject);
		Line.ReplaceInline(TEXT("{SECTOR}"), *SectorName.ToUpper());
		Line.ReplaceInline(TEXT("{PLAYER}"), *Player.ToUpper());
		Line.ReplaceInline(TEXT("{TIMING}"), *Grade.TimingLabel.ToString().ToUpper());
		return Line;
	}
}
