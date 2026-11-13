#include "Rules/DCCityPlan.h"

#include "DCTuning.h"

namespace DCCityRules
{
	int32 SectorSeed(int32 RunSeed, int32 SectorIndex)
	{
		return static_cast<int32>(HashCombine(GetTypeHash(RunSeed), GetTypeHash(SectorIndex * 7919 + 1)) & 0x7fffffff);
	}

	FDCCityPlan PlanCity(int32 Seed, const FDCCityTuning& T, int32 NumColors)
	{
		FDCCityPlan Plan;
		FRandomStream Rng(Seed);
		const float Pitch = T.LotSizeCm + T.StreetWidthCm;
		const float OffX = (T.GridX - 1) * Pitch * 0.5f;
		const float OffY = (T.GridY - 1) * Pitch * 0.5f;
		Plan.HalfExtent = FMath::Max(OffX, OffY) + Pitch * 0.5f;

		// Lots in a shuffled order; the first MinBuildings always get towers, the rest roll for plazas.
		TArray<FIntPoint> Lots;
		for (int32 X = 0; X < T.GridX; ++X)
		{
			for (int32 Y = 0; Y < T.GridY; ++Y)
			{
				Lots.Add(FIntPoint(X, Y));
			}
		}
		for (int32 i = Lots.Num() - 1; i > 0; --i)
		{
			Lots.Swap(i, Rng.RandRange(0, i));
		}
		const int32 Target = FMath::Clamp(Rng.RandRange(T.MinBuildings, T.MaxBuildings), 0, Lots.Num());
		for (int32 i = 0; i < Lots.Num() && Plan.Buildings.Num() < Target; ++i)
		{
			const bool bForced = Plan.Buildings.Num() < T.MinBuildings;
			if (!bForced && Rng.FRand() < T.EmptyLotChance)
			{
				continue;
			}
			FDCLotPlan& Lot = Plan.Buildings.AddDefaulted_GetRef();
			Lot.Location = FVector(Lots[i].X * Pitch - OffX, Lots[i].Y * Pitch - OffY, 0.f);
			Lot.Spec.Seed = Rng.RandHelper(MAX_int32);
			Lot.Spec.BlockSize = T.BlockSizeCm;
			const int32 MaxFit = FMath::Max(1, FMath::FloorToInt(T.LotSizeCm / T.BlockSizeCm));
			Lot.Spec.SizeX = FMath::Min(Rng.RandRange(T.MinFootprint, T.MaxFootprint), MaxFit);
			Lot.Spec.SizeY = FMath::Min(Rng.RandRange(T.MinFootprint, T.MaxFootprint), MaxFit);
			Lot.Spec.Floors = Rng.RandRange(T.MinFloors, T.MaxFloors);
			Lot.Spec.ColorIndex = static_cast<uint8>(NumColors > 0 ? Rng.RandRange(0, NumColors - 1) : 0);
		}

		// Street intersections between and around the lots.
		for (int32 X = 0; X <= T.GridX; ++X)
		{
			for (int32 Y = 0; Y <= T.GridY; ++Y)
			{
				Plan.StreetPoints.Add(FVector(X * Pitch - OffX - Pitch * 0.5f, Y * Pitch - OffY - Pitch * 0.5f, 0.f));
			}
		}
		return Plan;
	}
}
