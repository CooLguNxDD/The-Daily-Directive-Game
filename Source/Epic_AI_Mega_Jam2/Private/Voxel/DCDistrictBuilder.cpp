// City builder (R16) and prop stamping, on the PCG rules (R15). Pure: a seed and data rows in, a plan or voxels out.
#include "Rules/DCPcg.h"
#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelModel.h"

namespace
{
	constexpr uint32 SaltLot = 0x10751u;
	constexpr uint32 SaltRecipe = 0x7EC1Bu;
	constexpr uint32 SaltLandmark = 0x1A4D3u;

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

	/** Everything a prop placer needs (R2 props): the plan so far, the layer row and its two rule slots. */
	struct FPlaceCtx
	{
		FDCCityPlan& Plan;
		const FDCDistrictRule& D;
		const FDCPropLayer& Layer;
		const FDCPcgRule& Models;
		const FDCPcgRule& Count;
		const TArray<FDCVoxelRecipe>& Recipes;
		/** Lot kind of every building in Plan.Buildings. */
		const TArray<FName>& BuildingKinds;
		int32 Seed = 0;
		/** Per-layer salt (CRC of the layer id: the same on every machine). */
		uint32 Salt = 0;
		float Pitch = 0.f;
		float OffX = 0.f;
		float OffY = 0.f;
		float BayCm = 300.f;

		int32 LayerSeed() const { return Seed ^ static_cast<int32>(Salt); }
		float Unit(uint32 S, int32 Idx) const { return DCPcg::Unit(Seed, Salt ^ S, Idx); }
		int32 CountAt(int32 Key) const { return DCPcg::RangeInt(Count, LayerSeed(), Key); }
		FName ModelAt(int32 Idx) const { return DCPcg::Pick(Models, LayerSeed(), Idx); }
		bool UsesKind(FName Kind) const { return Layer.LotKinds.Num() == 0 || Layer.LotKinds.Contains(Kind); }
		void AddProp(int32 Idx, const FVector& At, int32 Turns) const
		{
			FDCPropPlan& P = Plan.Props.AddDefaulted_GetRef();
			P.Model = ModelAt(Idx);
			P.Location = FVector(FMath::RoundToFloat(At.X), FMath::RoundToFloat(At.Y), 0.f);
			P.Turns = Turns;
			P.Layer = Layer.Id;
		}
		void AddAttach(FDCLotPlan& B, int32 Idx, FName Mount, int32 Face, float U, float V) const
		{
			FDCAttachPlan& A = B.Attach.AddDefaulted_GetRef();
			A.Model = ModelAt(Idx);
			A.Layer = Layer.Id;
			A.Mount = Layer.Mount.IsNone() ? Mount : Layer.Mount;
			A.Face = Face;
			A.U = U;
			A.V = V;
		}
	};

	/** Outward face (-X, +X, -Y, +Y) -> quarter turns that point a model's +x out of it. */
	constexpr int32 FaceTurns[4] = {2, 0, 3, 1};
	const FVector FaceNormal[4] = {FVector(-1, 0, 0), FVector(1, 0, 0), FVector(0, -1, 0), FVector(0, 1, 0)};

	/** Calls Fn(Segment, Axis, Line, Mid) for every street segment between two crossings. */
	template <typename FnT>
	void ForEachSegment(const FPlaceCtx& C, FnT&& Fn)
	{
		int32 Segment = 0;
		const int32 Gx = C.Plan.GridX;
		const int32 Gy = C.Plan.GridY;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const int32 Lines = (Axis == 0 ? Gx : Gy) + 1;
			const int32 Spans = Axis == 0 ? Gy : Gx;
			for (int32 L = 0; L < Lines; ++L)
			{
				for (int32 S = 0; S < Spans; ++S, ++Segment)
				{
					const float Line = L * C.Pitch - (Axis == 0 ? C.OffX : C.OffY) - C.Pitch * 0.5f;
					const float Mid = S * C.Pitch - (Axis == 0 ? C.OffY : C.OffX);
					Fn(Segment, Axis, Line, Mid);
				}
			}
		}
	}

	FVector OnAxis(int32 Axis, float Across, float Along) { return Axis == 0 ? FVector(Across, Along, 0.f) : FVector(Along, Across, 0.f); }

	/** Placer registry (R2 props, rule 3): placer id -> how a layer's props are spread. A new placement is a row here. */
	using FPlacerFn = void (*)(const FPlaceCtx& C);
	const TMap<FName, FPlacerFn>& Placers()
	{
		static const TMap<FName, FPlacerFn> Table = {
			// Sidewalk props on either side of every street segment, clear of the crossings.
			{TEXT("Street"), [](const FPlaceCtx& C)
				{
					const float Side = C.D.StreetWidthCm * 0.5f - C.Layer.EdgeCm;
					ForEachSegment(C, [&C, Side](int32 Segment, int32 Axis, float Line, float Mid)
					{
						const int32 N = C.CountAt(Segment);
						for (int32 i = 0; i < N; ++i)
						{
							const int32 Idx = Segment * 32 + i;
							const float Along = Mid + (C.Unit(0x5u, Idx) - 0.5f) * C.D.LotSizeCm * C.Layer.SpreadPercent / 100.f;
							const bool bHigh = (DCPcg::Hash(C.Seed, C.Salt ^ 0x9u, Idx) & 1) != 0;
							const int32 Flip = static_cast<int32>(DCPcg::Hash(C.Seed, C.Salt ^ 0x3u, Idx) & 1);
							C.AddProp(Idx, OnAxis(Axis, Line + (bHigh ? Side : -Side), Along), Axis == 0 ? 1 + 2 * Flip : 2 * Flip);
						}
					});
				}},
			// Scattered in the lots of the layer's kinds (plazas, rubble zones), quarter turns from the hash.
			{TEXT("Lot"), [](const FPlaceCtx& C)
				{
					const int32 Gx = FMath::Max(1, C.Plan.GridX);
					const float Half = C.D.LotSizeCm * C.Layer.SpreadPercent / 200.f;
					for (int32 Cell = 0; Cell < C.Plan.LotKinds.Num(); ++Cell)
					{
						if (!C.Layer.LotKinds.Contains(C.Plan.LotKinds[Cell]))
						{
							continue;
						}
						const FVector Centre((Cell % Gx) * C.Pitch - C.OffX, (Cell / Gx) * C.Pitch - C.OffY, 0.f);
						const int32 N = C.CountAt(Cell);
						for (int32 i = 0; i < N; ++i)
						{
							const int32 Idx = Cell * 64 + i;
							const FVector At = Centre + FVector((C.Unit(1, Idx) * 2.f - 1.f) * Half, (C.Unit(2, Idx) * 2.f - 1.f) * Half, 0.f);
							C.AddProp(Idx, At, static_cast<int32>(DCPcg::Hash(C.Seed, C.Salt ^ 0x7u, Idx) % 4));
						}
					}
				}},
			// In front of each building face, EdgeCm off the wall, facing the street; kept inside the lot.
			{TEXT("Frontage"), [](const FPlaceCtx& C)
				{
					const float LotHalf = C.D.LotSizeCm * 0.5f - 60.f;
					for (int32 b = 0; b < C.Plan.Buildings.Num(); ++b)
					{
						if (!C.UsesKind(C.BuildingKinds[b]))
						{
							continue;
						}
						const FDCLotPlan& B = C.Plan.Buildings[b];
						const FVector2D H = FVector2D(B.Spec.SizeX, B.Spec.SizeY) * C.BayCm * 0.5f;
						for (int32 Face = 0; Face < 4; ++Face)
						{
							const int32 Key = b * 4 + Face;
							const bool bX = Face < 2;
							const float Out = FMath::Min((bX ? H.X : H.Y) + C.Layer.EdgeCm, LotHalf);
							const float Span = (bX ? H.Y : H.X) * C.Layer.SpreadPercent / 100.f;
							const int32 N = C.CountAt(Key);
							for (int32 i = 0; i < N; ++i)
							{
								const int32 Idx = Key * 16 + i;
								const float Along = (C.Unit(4, Idx) * 2.f - 1.f) * Span;
								C.AddProp(Idx, B.Location + FaceNormal[Face] * Out + (bX ? FVector(0, Along, 0) : FVector(Along, 0, 0)), FaceTurns[Face]);
							}
						}
					}
				}},
			// Roof clutter, crafted on the tower's roof deck (a recipe's RoofPropPercent scales it; 0 = no deck); the layer's
			// Mount may put it at the deck edge instead.
			{TEXT("Roof"), [](const FPlaceCtx& C)
				{
					for (int32 b = 0; b < C.Plan.Buildings.Num(); ++b)
					{
						FDCLotPlan& B = C.Plan.Buildings[b];
						const int32 Share = C.Recipes.IsValidIndex(B.Spec.Recipe) ? C.Recipes[B.Spec.Recipe].RoofPropPercent : 100;
						if (!C.UsesKind(C.BuildingKinds[b]) || Share <= 0)
						{
							continue;
						}
						const int32 N = C.CountAt(b) * Share / 100;
						for (int32 i = 0; i < N; ++i)
						{
							const int32 Idx = b * 32 + i;
							C.AddAttach(B, Idx, TEXT("Roof"), static_cast<int32>(DCPcg::Hash(C.Seed, C.Salt ^ 0xAu, Idx) % 4), C.Unit(0xBu, Idx), C.Unit(0xCu, Idx));
						}
					}
				}},
			// Wall mounts: per building face, spread along the face, at a floor height in [HeightMin, HeightMax].
			{TEXT("Wall"), [](const FPlaceCtx& C)
				{
					for (int32 b = 0; b < C.Plan.Buildings.Num(); ++b)
					{
						FDCLotPlan& B = C.Plan.Buildings[b];
						if (!C.UsesKind(C.BuildingKinds[b]))
						{
							continue;
						}
						for (int32 Face = 0; Face < 4; ++Face)
						{
							const int32 Key = b * 4 + Face;
							const int32 N = C.CountAt(Key);
							for (int32 i = 0; i < N; ++i)
							{
								const int32 Idx = Key * 16 + i;
								const float U = 0.5f + (C.Unit(0xDu, Idx) - 0.5f) * C.Layer.SpreadPercent / 100.f;
								C.AddAttach(B, Idx, TEXT("Wall"), Face, U, FMath::Lerp(C.Layer.HeightMin, C.Layer.HeightMax, C.Unit(0xEu, Idx)));
							}
						}
					}
				}},
			// Lines strung across the street between two sidewalk anchors (poles when the layer has one), at hashed
			// heights in [HeightMin, HeightMax] cm, slightly skewed so a street of them does not read as a ladder.
			{TEXT("Cable"), [](const FPlaceCtx& C)
				{
					const float Side = C.D.StreetWidthCm * 0.5f - C.Layer.EdgeCm;
					ForEachSegment(C, [&C, Side](int32 Segment, int32 Axis, float Line, float Mid)
					{
						const int32 N = C.CountAt(Segment);
						for (int32 i = 0; i < N; ++i)
						{
							const int32 Idx = Segment * 16 + i;
							const float Along = Mid + (C.Unit(0x11u, Idx) - 0.5f) * C.D.LotSizeCm * C.Layer.SpreadPercent / 100.f;
							const float Skew = (C.Unit(0x12u, Idx) - 0.5f) * C.D.StreetWidthCm * 0.4f;
							const FVector A = OnAxis(Axis, Line - Side, Along);
							const FVector B = OnAxis(Axis, Line + Side, Along + Skew);
							FDCCablePlan& K = C.Plan.Cables.AddDefaulted_GetRef();
							K.A = FVector(FMath::RoundToFloat(A.X), FMath::RoundToFloat(A.Y), FMath::RoundToFloat(FMath::Lerp(C.Layer.HeightMin, C.Layer.HeightMax, C.Unit(0x13u, Idx))));
							K.B = FVector(FMath::RoundToFloat(B.X), FMath::RoundToFloat(B.Y), FMath::RoundToFloat(FMath::Lerp(C.Layer.HeightMin, C.Layer.HeightMax, C.Unit(0x14u, Idx))));
							K.SagCm = C.Layer.SagCm;
							K.Material = C.Layer.Material;
							K.BeadMaterial = C.Layer.BeadMaterial;
							K.BeadEveryCm = C.Layer.BeadEveryCm;
							K.PoleMaterial = C.Layer.PoleMaterial;
						}
					});
				}},
		};
		return Table;
	}
}

namespace DCVoxelCraft
{
	FDCCityPlan PlanDistrict(int32 Seed, const FDCDistrictRule& D, const FDCPcgRuleSet* LotRules, const FDCPcgRuleSet* PropRules,
		const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, float BayCm, const TArray<FDCPropLayer>& PropLayers)
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

		// 2. The landmark: the lot nearest the centre (lowest index on a tie) when a landmark recipe exists. R23: the lot
		//    rules may roll it (slot LandmarkRecipe); the district row's LandmarkRecipe is the fallback.
		const FDCPcgRule* LandmarkPick = LotRules ? LotRules->Find(TEXT("LandmarkRecipe")) : nullptr;
		const int32 Rolled = LandmarkPick ? RecipeIndex(Recipes, DCPcg::Pick(*LandmarkPick, Seed ^ static_cast<int32>(SaltLandmark), 0)) : INDEX_NONE;
		const int32 LandmarkRecipe = Rolled != INDEX_NONE ? Rolled : RecipeIndex(Recipes, D.LandmarkRecipe);
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
		TArray<FName> BuildingKinds;
		auto AddBuilding = [&](int32 Cell, int32 Recipe, const FDCPcgRule* Floors)
		{
			if (!Recipes.IsValidIndex(Recipe))
			{
				return;
			}
			BuildingKinds.Add(Plan.LotKinds[Cell]);
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
			{TEXT("Plaza"), [&](int32 Cell) { Plan.Plazas.Add(Centre(Cell)); }},
			{TEXT("Rubble"), [&](int32 Cell) { Plan.RubbleZones.Add(Centre(Cell)); }},
		};
		for (int32 c = 0; c < Gx * Gy; ++c)
		{
			if (const FLotFn* Fn = Kinds.Find(Plan.LotKinds[c]))
			{
				(*Fn)(c);
			}
		}

		// 6. Props (R2): every prop layer the district's prop rule set turns on (slots <Id> and <Id>Count), through the
		//    placer registry.
		for (const FDCPropLayer& Layer : PropLayers)
		{
			const FDCPcgRule* Models = PropRules ? PropRules->Find(Layer.Id) : nullptr;
			const FDCPcgRule* Count = PropRules ? PropRules->Find(FName(*(Layer.Id.ToString() + TEXT("Count")))) : nullptr;
			const FPlacerFn* Placer = Placers().Find(Layer.Placer);
			if (!Models || !Count || !Placer)
			{
				continue;
			}
			const FPlaceCtx C{Plan, D, Layer, *Models, *Count, Recipes, BuildingKinds, Seed, FCrc::StrCrc32(*Layer.Id.ToString()), Pitch, OffX, OffY, BayCm};
			(*Placer)(C);
		}
		// Keep the spawn street clear: nothing stands in the players' faces on the first frame (critic).
		const FVector Spawn = Plan.SightFrom;
		Plan.Props.RemoveAll([&Spawn](const FDCPropPlan& P) { return P.Model.IsNone() || FVector::Dist2D(P.Location, Spawn) < 1500.f; });
		Plan.Cables.RemoveAll([&Spawn](const FDCCablePlan& K) { return FVector::Dist2D(K.A, Spawn) < 1500.f || FVector::Dist2D(K.B, Spawn) < 1500.f; });
		for (FDCLotPlan& B : Plan.Buildings)
		{
			B.Attach.RemoveAll([](const FDCAttachPlan& A) { return A.Model.IsNone(); });
		}
		return Plan;
	}

	int32 StampModel(FDCVoxelWorld& W, const FDCVoxelModel& M, int32 TargetCm, const FIntVector& Base, int32 Turns, const FDCVoxelCraftContext& Ctx, bool bOnlyAir)
	{
		// Slot -> palette id, by name.
		TArray<uint8> SlotIds;
		SlotIds.Add(0);
		for (const FName& Slot : M.Slots)
		{
			SlotIds.Add(Ctx.MaterialId(Slot));
		}
		const int32 V = FMath::Max(1, TargetCm);
		// A 10 cm model fills 2^3 voxels of a 5 cm grid (K); a 5 cm model in a 10 cm grid keeps every second voxel (Step).
		const int32 K = FMath::Max(1, M.GridCm / V);
		const int32 Step = FMath::Max(1, V / FMath::Max(1, M.GridCm));
		const int32 T = ((Turns % 4) + 4) % 4;
		int32 Top = Base.Z;
		for (int32 Z = 0; Z < M.Size.Z; Z += Step)
		{
			for (int32 Y = 0; Y < M.Size.Y; Y += Step)
			{
				for (int32 X = 0; X < M.Size.X; X += Step)
				{
					const uint8 S = M.Cells[X + M.Size.X * (Y + M.Size.Y * Z)];
					if (S == 0)
					{
						continue;
					}
					// Quarter turn about the pivot: (x, y) -> (-y, x) per turn.
					FIntPoint D(DCVoxelMath::FloorDiv(X - M.Pivot.X, Step), DCVoxelMath::FloorDiv(Y - M.Pivot.Y, Step));
					for (int32 t = 0; t < T; ++t)
					{
						D = FIntPoint(-D.Y, D.X);
					}
					const FIntVector Cell = Base + FIntVector(D.X * K, D.Y * K, DCVoxelMath::FloorDiv(Z - M.Pivot.Z, Step) * K);
					const uint8 Mat = SlotIds.IsValidIndex(S) ? SlotIds[S] : 1;
					Top = FMath::Max(Top, Cell.Z + K);
					if (!bOnlyAir)
					{
						W.FillBox(Cell, Cell + FIntVector(K), Mat);
						continue;
					}
					for (int32 dz = 0; dz < K; ++dz)
					{
						for (int32 dy = 0; dy < K; ++dy)
						{
							for (int32 dx = 0; dx < K; ++dx)
							{
								const FIntVector C = Cell + FIntVector(dx, dy, dz);
								if (!W.IsSolid(C))
								{
									W.Set(C, Mat);
								}
							}
						}
					}
				}
			}
		}
		return Top;
	}

	TArray<FName> PlanModels(const FDCCityPlan& Plan)
	{
		TArray<FName> Out;
		for (const FDCPropPlan& P : Plan.Props)
		{
			Out.AddUnique(P.Model);
		}
		for (const FDCLotPlan& B : Plan.Buildings)
		{
			for (const FDCAttachPlan& A : B.Attach)
			{
				Out.AddUnique(A.Model);
			}
		}
		return Out;
	}

	void CraftCable(FDCVoxelGrid& Out, const FDCCablePlan& K, const FDCVoxelCraftContext& Ctx)
	{
		const int32 V = FMath::Max(1, Out.VoxelCm);
		const uint8 Line = Ctx.MaterialId(K.Material);
		const int32 Thick = FMath::Max(1, 10 / V); // about 10 cm: reads at street distance
		// Poles under both ends, a voxel or two above the anchor.
		if (!K.PoleMaterial.IsNone())
		{
			const uint8 Pole = Ctx.MaterialId(K.PoleMaterial);
			const int32 PoleW = FMath::Max(1, 15 / V);
			for (const FVector& End : {K.A, K.B})
			{
				const FIntVector Foot = Out.ToLocal(DCVoxelMath::ToCm(FVector(End.X, End.Y, 0.f)));
				const FIntVector Head = Out.ToLocal(DCVoxelMath::ToCm(End));
				Out.Data.FillBox(Foot - FIntVector(PoleW / 2, PoleW / 2, 0), FIntVector(Foot.X - PoleW / 2 + PoleW, Foot.Y - PoleW / 2 + PoleW, Head.Z + 2 * Thick), Pole);
				Out.Data.FillBox(Head - FIntVector(PoleW, PoleW, 0) + FIntVector(0, 0, 2 * Thick), Head + FIntVector(PoleW, PoleW, 2 * Thick + 1), Pole);
			}
		}
		// The line: a parabola sagging SagCm at mid span, sampled every half voxel.
		const float Len = FVector::Dist(K.A, K.B);
		const int32 Steps = FMath::Max(2, FMath::CeilToInt32(Len / (V * 0.5f)));
		const uint8 Bead = K.BeadMaterial.IsNone() ? 0 : Ctx.MaterialId(K.BeadMaterial);
		const int32 BeadEvery = K.BeadEveryCm > 0.f ? FMath::Max(1, FMath::RoundToInt32(K.BeadEveryCm / (V * 0.5f))) : 0;
		const int32 BeadSize = FMath::Max(2, 15 / V);
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float T = static_cast<float>(i) / Steps;
			const FVector P = FMath::Lerp(K.A, K.B, T) - FVector(0.f, 0.f, K.SagCm * 4.f * T * (1.f - T));
			const FIntVector Cell = Out.ToLocal(DCVoxelMath::ToCm(P));
			Out.Data.FillBox(Cell, Cell + FIntVector(Thick), Line);
			if (Bead && BeadEvery > 0 && i > 0 && i < Steps && i % BeadEvery == 0)
			{
				const FIntVector B0 = Cell - FIntVector(BeadSize / 2, BeadSize / 2, BeadSize);
				Out.Data.FillBox(B0, B0 + FIntVector(BeadSize), Bead);
			}
		}
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
			if (Found && *Found)
			{
				StampModel(Out.Data, **Found, V, Out.ToLocal(DCVoxelMath::ToCm(P.Location)), P.Turns, Ctx, false);
			}
		}
		for (const FDCCablePlan& K : Plan.Cables)
		{
			CraftCable(Out, K, Ctx);
		}
	}
}
