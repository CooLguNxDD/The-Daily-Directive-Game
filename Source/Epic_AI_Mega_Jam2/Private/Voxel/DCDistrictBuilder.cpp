// City builder (R16) and prop stamping, on the PCG rules (R15). Pure: a seed and data rows in, a plan or voxels out.
#include "Rules/DCPcg.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelModel.h"

namespace
{
	constexpr uint32 SaltLot = 0x10751u;
	constexpr uint32 SaltProp = 0x960Fu;
	constexpr uint32 SaltRecipe = 0x7EC1Bu;

	/** Rolls footprint and floors inside a recipe's ranges (floor range overridable by a PCG rule). */
	void RollSpec(FDCLotPlan& Lot, int32 RecipeIndex, const FDCVoxelRecipe& R, int32 FitBays, const FDCPcgRule* Floors, int32 Seed, int32 Cell)
	{
		FRandomStream Rng(Lot.Spec.Seed ^ 0x7EC1BE);
		Lot.Spec.Recipe = static_cast<uint8>(RecipeIndex);
		Lot.Spec.SizeX = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, FMath::Max(2, FitBays));
		Lot.Spec.SizeY = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, FMath::Max(2, FitBays));
		Lot.Spec.Floors = Floors ? FMath::Max(2, DCPcg::RangeInt(*Floors, Seed, Cell)) : FMath::Max(2, Rng.RandRange(R.MinFloors, R.MaxFloors));
	}

	int32 RecipeIndex(const TArray<FDCVoxelRecipe>& Recipes, FName Id)
	{
		return Recipes.IndexOfByPredicate([Id](const FDCVoxelRecipe& R) { return R.Id == Id; });
	}

	int32 WeightedRecipe(const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, int32 Seed, int32 Cell)
	{
		FDCPcgRule Rule;
		Rule.Slot = TEXT("Recipe");
		for (const FDCVoxelRecipeWeight& W : Layout.Recipes)
		{
			Rule.Choices.Add({W.Recipe, W.Weight});
		}
		const int32 Index = RecipeIndex(Recipes, DCPcg::Pick(Rule, Seed, Cell));
		return Index == INDEX_NONE ? 0 : Index;
	}

	/** Places Count props of a slot's models at hashed spots inside a square (quarter turns from the hash). */
	void ScatterProps(FDCCityPlan& Plan, const FDCPcgRuleSet* Rules, FName Slot, FName CountSlot, const FVector& Centre, float Half, int32 Seed, int32 Key)
	{
		const FDCPcgRule* Models = Rules ? Rules->Find(Slot) : nullptr;
		const FDCPcgRule* Count = Rules ? Rules->Find(CountSlot) : nullptr;
		if (!Models || !Count)
		{
			return;
		}
		const int32 N = DCPcg::RangeInt(*Count, Seed, Key);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Idx = Key * 64 + i;
			FDCPropPlan& P = Plan.Props.AddDefaulted_GetRef();
			P.Model = DCPcg::Pick(*Models, Seed, Idx);
			P.Location = Centre + FVector((DCPcg::Unit(Seed, SaltProp, Idx * 2) * 2.f - 1.f) * Half, (DCPcg::Unit(Seed, SaltProp, Idx * 2 + 1) * 2.f - 1.f) * Half, 0.f);
			P.Location = FVector(FMath::RoundToFloat(P.Location.X), FMath::RoundToFloat(P.Location.Y), 0.f);
			P.Turns = static_cast<int32>(DCPcg::Hash(Seed, SaltProp ^ 0x7u, Idx) % 4);
		}
	}
}

namespace DCVoxelCraft
{
	FDCCityPlan PlanDistrict(int32 Seed, const FDCDistrictRule& D, const FDCPcgRuleSet* LotRules, const FDCPcgRuleSet* PropRules,
		const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, float BayCm)
	{
		FDCCityPlan Plan;
		const int32 Gx = FMath::Max(1, D.GridX), Gy = FMath::Max(1, D.GridY);
		Plan.GridX = Gx;
		Plan.GridY = Gy;
		Plan.LotSizeCm = D.LotSizeCm;
		const float Pitch = D.LotSizeCm + D.StreetWidthCm;
		const float OffX = (Gx - 1) * Pitch * 0.5f;
		const float OffY = (Gy - 1) * Pitch * 0.5f;
		Plan.HalfExtent = FMath::Max(OffX, OffY) + Pitch * 0.5f;
		for (int32 X = 0; X <= Gx; ++X)
		{
			for (int32 Y = 0; Y <= Gy; ++Y)
			{
				Plan.StreetPoints.Add(FVector(X * Pitch - OffX - Pitch * 0.5f, Y * Pitch - OffY - Pitch * 0.5f, 0.f));
			}
		}
		auto Centre = [&](int32 Cell) { return FVector((Cell % Gx) * Pitch - OffX, (Cell / Gx) * Pitch - OffY, 0.f); };

		// 1. Lot kinds from the PCG rules (weights, adjacency, symmetry).
		const FDCPcgRule* LotRule = LotRules ? LotRules->Find(TEXT("Lot")) : nullptr;
		Plan.LotKinds = LotRule ? DCPcg::PickGrid(*LotRule, Seed, Gx, Gy) : TArray<FName>();
		Plan.LotKinds.SetNum(Gx * Gy);
		for (FName& K : Plan.LotKinds)
		{
			K = K.IsNone() ? FName(TEXT("Tower")) : K;
		}

		// 2. The landmark: the lot nearest the centre (lowest index on a tie) when a landmark recipe exists.
		const int32 LandmarkRecipe = RecipeIndex(Recipes, D.LandmarkRecipe);
		int32 LandmarkCell = INDEX_NONE;
		if (LandmarkRecipe != INDEX_NONE)
		{
			float Best = TNumericLimits<float>::Max();
			for (int32 c = 0; c < Gx * Gy; ++c)
			{
				const float Dist = Centre(c).Size2D() + c * 0.01f;
				if (Dist < Best)
				{
					Best = Dist;
					LandmarkCell = c;
				}
			}
			Plan.LotKinds[LandmarkCell] = TEXT("Landmark");
		}

		// 3. Sight line: the players spawn on the west street (GameMode); lots the line to the landmark crosses stay
		//    open or low so the landmark is in view from the first second.
		Plan.SightFrom = FVector(-Plan.HalfExtent - 600.f, 0.f, 170.f);
		if (LandmarkCell != INDEX_NONE)
		{
			Plan.SightTo = Centre(LandmarkCell);
			for (int32 c = 0; c < Gx * Gy; ++c)
			{
				if (c == LandmarkCell)
				{
					continue;
				}
				const FVector P = Centre(c);
				const float T = FMath::Clamp(FVector::DotProduct(P - Plan.SightFrom, Plan.SightTo - Plan.SightFrom) / FMath::Max((Plan.SightTo - Plan.SightFrom).SizeSquared2D(), 1.f), 0.f, 1.f);
				const FVector Closest = Plan.SightFrom + (Plan.SightTo - Plan.SightFrom) * T;
				if (T > 0.f && T < 1.f && FVector::Dist2D(P, Closest) < D.LotSizeCm * 0.5f)
				{
					Plan.LotKinds[c] = D.SightLineKind;
				}
			}
		}

		// 4. Enough towers to knock down: open lots become towers, in hashed order, until MinTowers.
		static const TSet<FName> Built = {TEXT("Tower"), TEXT("Low"), TEXT("Landmark")};
		int32 Towers = 0;
		for (const FName& K : Plan.LotKinds)
		{
			Towers += Built.Contains(K) ? 1 : 0;
		}
		TArray<int32> Open;
		for (int32 c = 0; c < Gx * Gy; ++c)
		{
			if (!Built.Contains(Plan.LotKinds[c]))
			{
				Open.Add(c);
			}
		}
		Open.Sort([Seed](int32 A, int32 B) { return DCPcg::Hash(Seed, SaltLot, A) < DCPcg::Hash(Seed, SaltLot, B); });
		for (int32 i = 0; i < Open.Num() && Towers < D.MinTowers; ++i, ++Towers)
		{
			Plan.LotKinds[Open[i]] = TEXT("Tower");
		}

		// 5. Each lot by kind (a table, not a ladder).
		const int32 Fit = FMath::Max(2, FMath::FloorToInt(D.LotSizeCm / FMath::Max(BayCm, 1.f)));
		const FDCPcgRule* LowRecipe = LotRules ? LotRules->Find(TEXT("LowRecipe")) : nullptr;
		const FDCPcgRule* LowFloors = LotRules ? LotRules->Find(TEXT("LowFloors")) : nullptr;
		const FDCPcgRule* LandmarkFloors = LotRules ? LotRules->Find(TEXT("LandmarkFloors")) : nullptr;
		using FLotFn = TFunction<void(int32 Cell)>;
		auto AddBuilding = [&](int32 Cell, int32 Recipe, const FDCPcgRule* Floors)
		{
			if (!Recipes.IsValidIndex(Recipe))
			{
				return;
			}
			FDCLotPlan& Lot = Plan.Buildings.AddDefaulted_GetRef();
			Lot.Location = Centre(Cell);
			Lot.Spec.Seed = static_cast<int32>(DCPcg::Hash(Seed, SaltLot, Cell) & 0x7fffffff);
			Lot.Spec.BlockSize = BayCm;
			RollSpec(Lot, Recipe, Recipes[Recipe], Fit, Floors, Seed, Cell);
		};
		const TMap<FName, FLotFn> Kinds = {
			{TEXT("Tower"), [&](int32 Cell) { AddBuilding(Cell, WeightedRecipe(Layout, Recipes, Seed ^ SaltRecipe, Cell), nullptr); }},
			{TEXT("Low"), [&](int32 Cell)
				{
					const int32 R = LowRecipe ? RecipeIndex(Recipes, DCPcg::Pick(*LowRecipe, Seed, Cell)) : INDEX_NONE;
					AddBuilding(Cell, R == INDEX_NONE ? WeightedRecipe(Layout, Recipes, Seed ^ SaltRecipe, Cell) : R, LowFloors);
				}},
			{TEXT("Landmark"), [&](int32 Cell)
				{
					Plan.Landmark = Plan.Buildings.Num();
					AddBuilding(Cell, LandmarkRecipe, LandmarkFloors);
				}},
			{TEXT("Plaza"), [&](int32 Cell)
				{
					Plan.Plazas.Add(Centre(Cell));
					ScatterProps(Plan, PropRules, TEXT("Plaza"), TEXT("PlazaCount"), Centre(Cell), D.LotSizeCm * 0.35f, Seed, Cell);
				}},
			{TEXT("Rubble"), [&](int32 Cell)
				{
					Plan.RubbleZones.Add(Centre(Cell));
					ScatterProps(Plan, PropRules, TEXT("Rubble"), TEXT("RubbleCount"), Centre(Cell), D.LotSizeCm * 0.4f, Seed, Cell);
				}},
		};
		for (int32 c = 0; c < Gx * Gy; ++c)
		{
			if (const FLotFn* Fn = Kinds.Find(Plan.LotKinds[c]))
			{
				(*Fn)(c);
			}
		}

		// 6. Street props on the sidewalk side of every street segment, clear of the crossings.
		const FDCPcgRule* StreetModels = PropRules ? PropRules->Find(TEXT("Street")) : nullptr;
		const FDCPcgRule* StreetCount = PropRules ? PropRules->Find(TEXT("StreetCount")) : nullptr;
		if (StreetModels && StreetCount)
		{
			int32 Segment = 0;
			const float Side = D.StreetWidthCm * 0.5f - 180.f;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const int32 Lines = (Axis == 0 ? Gx : Gy) + 1;
				const int32 Spans = Axis == 0 ? Gy : Gx;
				for (int32 L = 0; L < Lines; ++L)
				{
					for (int32 S = 0; S < Spans; ++S, ++Segment)
					{
						const float Line = L * Pitch - (Axis == 0 ? OffX : OffY) - Pitch * 0.5f;
						const float Mid = S * Pitch - (Axis == 0 ? OffY : OffX);
						const int32 N = DCPcg::RangeInt(*StreetCount, Seed, Segment);
						for (int32 i = 0; i < N; ++i)
						{
							const int32 Idx = Segment * 32 + i;
							const float Along = Mid + (DCPcg::Unit(Seed, SaltProp ^ 0x5u, Idx) - 0.5f) * D.LotSizeCm * 0.8f;
							const float Across = Line + ((DCPcg::Hash(Seed, SaltProp ^ 0x9u, Idx) & 1) ? Side : -Side);
							FDCPropPlan& P = Plan.Props.AddDefaulted_GetRef();
							P.Model = DCPcg::Pick(*StreetModels, Seed, Idx);
							P.Location = Axis == 0 ? FVector(FMath::RoundToFloat(Across), FMath::RoundToFloat(Along), 0.f) : FVector(FMath::RoundToFloat(Along), FMath::RoundToFloat(Across), 0.f);
							P.Turns = Axis == 0 ? 1 + 2 * static_cast<int32>(DCPcg::Hash(Seed, SaltProp ^ 0x3u, Idx) & 1) : 2 * static_cast<int32>(DCPcg::Hash(Seed, SaltProp ^ 0x3u, Idx) & 1);
						}
					}
				}
			}
		}
		Plan.Props.RemoveAll([](const FDCPropPlan& P) { return P.Model.IsNone(); });
		return Plan;
	}

	void CraftProps(FDCVoxelGrid& Out, const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx)
	{
		const int32 V = FMath::Max(1, Ctx.Tuning ? Ctx.Tuning->PropVoxelCm : 5);
		Out.Data.Reset();
		Out.VoxelCm = V;
		const int32 Half = FMath::RoundToInt32(Plan.HalfExtent) + (Ctx.Tuning ? Ctx.Tuning->TerrainMarginCm : 2000);
		Out.OriginCm = FIntVector(-Half, -Half, 0);
		for (const FDCPropPlan& P : Plan.Props)
		{
			const FDCVoxelModel* const* Found = Ctx.Models.Find(P.Model);
			if (!Found || !*Found)
			{
				continue;
			}
			const FDCVoxelModel& M = **Found;
			// Slot -> palette id, by name.
			TArray<uint8> SlotIds;
			SlotIds.Add(0);
			for (const FName& Slot : M.Slots)
			{
				SlotIds.Add(Ctx.MaterialId(Slot));
			}
			const int32 K = FMath::Max(1, M.GridCm / V); // a 10 cm model fills 2^3 prop voxels per model voxel
			const FIntVector Base = Out.ToLocal(DCVoxelMath::ToCm(P.Location));
			const int32 Turns = ((P.Turns % 4) + 4) % 4;
			for (int32 Z = 0; Z < M.Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < M.Size.Y; ++Y)
				{
					for (int32 X = 0; X < M.Size.X; ++X)
					{
						const uint8 S = M.Cells[X + M.Size.X * (Y + M.Size.Y * Z)];
						if (S == 0)
						{
							continue;
						}
						// Quarter turn about the pivot: (x, y) -> (-y, x) per turn.
						FIntPoint D(X - M.Pivot.X, Y - M.Pivot.Y);
						for (int32 t = 0; t < Turns; ++t)
						{
							D = FIntPoint(-D.Y, D.X);
						}
						const FIntVector Cell = Base + FIntVector(D.X * K, D.Y * K, (Z - M.Pivot.Z) * K);
						Out.Data.FillBox(Cell, Cell + FIntVector(K), SlotIds.IsValidIndex(S) ? SlotIds[S] : 1);
					}
				}
			}
		}
	}
}
