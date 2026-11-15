#include "Voxel/DCVoxelCraft.h"

#include "Voxel/DCVoxelWorld.h"
#include "Voxel/DCVoxelOps.h"
#include "Voxel/DCVoxelModel.h"
#include "Async/ParallelFor.h"
#include "Rules/DCPcg.h"

uint8 FDCVoxelCraftContext::MaterialId(FName Id) const
{
	const int32 Index = Palette ? Palette->IndexOfByPredicate([Id](const FDCVoxelMaterial& M) { return M.Id == Id; }) : INDEX_NONE;
	return Index == INDEX_NONE ? 1 : static_cast<uint8>(FMath::Min(Index + 1, 255));
}

const FDCVoxelRecipe* FDCVoxelCraftContext::Recipe(int32 Index) const
{
	if (!Recipes || Recipes->Num() == 0)
	{
		return nullptr;
	}
	return &(*Recipes)[FMath::Clamp(Index, 0, Recipes->Num() - 1)];
}

namespace
{
	/** What a facade cell becomes: solid wall, a recessed window (outer voxel air, inner glass) or an opening. */
	enum class EOpening : uint8 { Solid, Window, Open };

	/** Facade bay registry: pattern char -> which cells of the bay open. O = offset across the bay, H = height in the
	 *  floor, B = bay size, Sill/Win from the recipe. Adding a bay kind is a new row here. */
	using FBayFn = EOpening (*)(int32 O, int32 H, int32 B, int32 Sill, int32 Win);
	const TMap<TCHAR, FBayFn>& Bays()
	{
		// Sizes are fractions of the bay, so a recipe keeps its proportions at any voxel size.
		static const TMap<TCHAR, FBayFn> Table = {
			{TEXT('P'), [](int32, int32, int32, int32, int32) { return EOpening::Solid; }},
			{TEXT('W'), [](int32 O, int32 H, int32 B, int32 Sill, int32 Win)
				{ return O >= B / 6 && O < B - B / 6 && H >= Sill && H < Sill + Win ? EOpening::Window : EOpening::Solid; }},
			{TEXT('S'), [](int32 O, int32 H, int32 B, int32, int32)
				{ return O >= B / 2 - B / 12 && O < B / 2 + B / 12 && H >= B / 10 && H < B - B / 10 ? EOpening::Window : EOpening::Solid; }},
			{TEXT('G'), [](int32 O, int32 H, int32 B, int32 Sill, int32 Win)
				{ const bool bCol = (O >= B / 6 && O < B / 2 - B / 15) || (O >= B / 2 + B / 15 && O < B - B / 6); return bCol && H >= Sill && H < Sill + Win ? EOpening::Window : EOpening::Solid; }},
			{TEXT('D'), [](int32 O, int32 H, int32 B, int32, int32)
				{ return O >= B / 4 && O < B - B / 4 && H >= 1 && H < B * 2 / 3 ? EOpening::Open : EOpening::Solid; }},
			// R23: a ribbon window (the whole bay, half the window height), twin tall slots, and a storefront (ground
			// floors: glass nearly floor to ceiling).
			{TEXT('R'), [](int32 O, int32 H, int32 B, int32 Sill, int32 Win)
				{ return O >= B / 12 && O < B - B / 12 && H >= Sill && H < Sill + FMath::Max(1, Win / 2) ? EOpening::Window : EOpening::Solid; }},
			{TEXT('T'), [](int32 O, int32 H, int32 B, int32, int32)
				{ const int32 Half = FMath::Max(1, B / 20); const bool bSlot = FMath::Abs(O - B / 3) <= Half || FMath::Abs(O - 2 * B / 3) <= Half; return bSlot && H >= B / 10 && H < B - B / 10 ? EOpening::Window : EOpening::Solid; }},
			{TEXT('X'), [](int32 O, int32 H, int32 B, int32, int32)
				{ return O >= B / 12 && O < B - B / 12 && H >= 2 && H < B * 3 / 4 ? EOpening::Window : EOpening::Solid; }},
		};
		return Table;
	}

	struct FFaceFrame
	{
		FIntVector Min;
		FIntVector Max;
		/** Cell of a face at depth D (0 = outer layer, -1 = one voxel proud of the wall), along-face U, height Z. */
		FIntVector Cell(int32 Face, int32 D, int32 U, int32 Z) const
		{
			// Faces 0/1 face -X/+X, faces 2/3 face -Y/+Y.
			const int32 NAxis = Face < 2 ? 0 : 1;
			const int32 UAxis = 1 - NAxis;
			FIntVector P(0, 0, Z);
			P[NAxis] = (Face % 2 == 0) ? Min[NAxis] + D : Max[NAxis] - 1 - D;
			P[UAxis] = Min[UAxis] + U;
			return P;
		}
		int32 Length(int32 Face) const { return Face < 2 ? Max.Y - Min.Y : Max.X - Min.X; }
	};

	struct FCraftIds
	{
		uint8 Wall = 1, Window = 1, Trim = 1, Slab = 1, Poster = 1, Ink = 1, PosterEdge = 1, WindowOff = 0, WindowAlt = 0;
		/** About 25 cm in voxels: the roof and detail builders size their parts in units. */
		int32 Unit = 1;
	};

	void RingLayer(FDCVoxelWorld& W, const FFaceFrame& F, int32 Z, int32 Depth, uint8 Mat, int32 Reach = 1)
	{
		for (int32 Face = 0; Face < 4; ++Face)
		{
			for (int32 U = -Reach; U < F.Length(Face) + Reach; ++U)
			{
				W.Set(F.Cell(Face, Depth, U, Z), Mat);
			}
		}
	}

	/** Roof registry: builds above TopZ inside the top floor's box, returns the highest z written + 1. */
	using FRoofFn = int32 (*)(FDCVoxelWorld&, const FFaceFrame&, int32 TopZ, const FCraftIds&, FRandomStream&);

	int32 Parapet(FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids)
	{
		const int32 U = Ids.Unit;
		for (int32 Z = TopZ + 1; Z < TopZ + 1 + 3 * U; ++Z)
		{
			RingLayer(W, F, Z, 0, Ids.Wall);
		}
		for (int32 Z = TopZ + 1 + 3 * U; Z < TopZ + 1 + 4 * U; ++Z)
		{
			RingLayer(W, F, Z, 0, Ids.Trim);
		}
		return TopZ + 1 + 4 * U;
	}

	/** A mast Unit thick, Height units tall, with Bars cross bars. */
	int32 Mast(FDCVoxelWorld& W, const FIntVector& Base, int32 Height, int32 Bars, uint8 Mat, int32 U)
	{
		const int32 H = Height * U;
		W.FillBox(Base, Base + FIntVector(U, U, H), Mat);
		for (int32 B = 1; B <= Bars; ++B)
		{
			const int32 Z = Base.Z + H * B / (Bars + 1);
			W.FillBox(FIntVector(Base.X - 2 * U, Base.Y, Z), FIntVector(Base.X + 3 * U, Base.Y + U, Z + U), Mat);
		}
		return Base.Z + H;
	}

	const TMap<FName, FRoofFn>& Roofs()
	{
		static const TMap<FName, FRoofFn> Table = {
			{TEXT("Flat"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream& Rng)
				{
					const int32 U = Ids.Unit;
					const int32 Top = Parapet(W, F, TopZ, Ids);
					// A stair hut somewhere on the roof.
					const int32 Hut = 9 * U;
					const int32 X = Rng.RandRange(F.Min.X + 3 * U, FMath::Max(F.Min.X + 3 * U, F.Max.X - Hut - 3 * U));
					const int32 Y = Rng.RandRange(F.Min.Y + 3 * U, FMath::Max(F.Min.Y + 3 * U, F.Max.Y - Hut - 3 * U));
					W.FillBox(FIntVector(X, Y, TopZ + 1), FIntVector(X + Hut, Y + Hut, TopZ + 1 + Hut), Ids.Wall);
					W.FillBox(FIntVector(X - U, Y - U, TopZ + 1 + Hut), FIntVector(X + Hut + U, Y + Hut + U, TopZ + 1 + Hut + U), Ids.Trim);
					return FMath::Max(Top, TopZ + 1 + Hut + U);
				}},
			{TEXT("Crown"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					int32 Z = TopZ + 1;
					for (int32 Step = 1; Step <= 3; ++Step)
					{
						const FIntVector In(Step * 3 * U, Step * 3 * U, 0);
						if (F.Max.X - F.Min.X <= In.X * 2 + 2 * U || F.Max.Y - F.Min.Y <= In.Y * 2 + 2 * U)
						{
							break;
						}
						W.FillBox(FIntVector(F.Min.X, F.Min.Y, Z) + In, FIntVector(F.Max.X, F.Max.Y, Z + 4 * U) - In, Step == 3 ? Ids.Trim : Ids.Wall);
						Z += 4 * U;
					}
					const FIntVector Mid((F.Min.X + F.Max.X) / 2, (F.Min.Y + F.Max.Y) / 2, Z);
					return Mast(W, Mid, 16, 1, Ids.Trim, U);
				}},
			{TEXT("Spire"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					int32 Z = TopZ + 1;
					for (int32 Level = 1;; ++Level)
					{
						const int32 In = Level * 2 * U;
						if (F.Max.X - F.Min.X <= In * 2 + U || F.Max.Y - F.Min.Y <= In * 2 + U)
						{
							break;
						}
						W.FillBox(FIntVector(F.Min.X + In, F.Min.Y + In, Z), FIntVector(F.Max.X - In, F.Max.Y - In, Z + 3 * U), Level % 3 == 0 ? Ids.Trim : Ids.Wall);
						Z += 3 * U;
					}
					const FIntVector Mid((F.Min.X + F.Max.X) / 2, (F.Min.Y + F.Max.Y) / 2, Z);
					return Mast(W, Mid, 12, 0, Ids.Trim, U);
				}},
			{TEXT("Antenna"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream& Rng)
				{
					const int32 U = Ids.Unit;
					const int32 Top = Parapet(W, F, TopZ, Ids);
					const FIntVector Base(Rng.RandRange(F.Min.X + 4 * U, F.Max.X - 5 * U), Rng.RandRange(F.Min.Y + 4 * U, F.Max.Y - 5 * U), TopZ + 1);
					return FMath::Max(Top, Mast(W, Base, 30, 2, Ids.Trim, U));
				}},
			// R23 roof kinds. Tiered: stacked eaves (a pagoda-like crown), each tier a smaller box under a trim eave one
			// unit wider than it; a short finial on top.
			{TEXT("Tiered"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					int32 Z = TopZ + 1;
					for (int32 Tier = 0; Tier < 4; ++Tier)
					{
						const int32 In = Tier * 4 * U;
						if (F.Max.X - F.Min.X <= In * 2 + 4 * U || F.Max.Y - F.Min.Y <= In * 2 + 4 * U)
						{
							break;
						}
						const FIntVector Lo(F.Min.X + In, F.Min.Y + In, 0), Hi(F.Max.X - In, F.Max.Y - In, 0);
						W.FillBox(FIntVector(Lo.X - U, Lo.Y - U, Z), FIntVector(Hi.X + U, Hi.Y + U, Z + U), Ids.Trim);
						// Each tier is a shell (walls one unit thick): the inside stays air, cheap to mesh and to break.
						W.FillBox(FIntVector(Lo.X + U, Lo.Y + U, Z + U), FIntVector(Hi.X - U, Hi.Y - U, Z + 5 * U), Ids.Wall);
						W.FillBox(FIntVector(Lo.X + 2 * U, Lo.Y + 2 * U, Z + U), FIntVector(Hi.X - 2 * U, Hi.Y - 2 * U, Z + 5 * U - 1), 0);
						Z += 5 * U;
					}
					W.FillBox(FIntVector((F.Min.X + F.Max.X) / 2 - U, (F.Min.Y + F.Max.Y) / 2 - U, Z), FIntVector((F.Min.X + F.Max.X) / 2 + U, (F.Min.Y + F.Max.Y) / 2 + U, Z + U), Ids.Trim);
					return Mast(W, FIntVector((F.Min.X + F.Max.X) / 2, (F.Min.Y + F.Max.Y) / 2, Z + U), 6, 0, Ids.Trim, U);
				}},
			// Dome: a voxel half-ellipsoid over the deck with a thin neon ring (the sign edge colour) at its foot.
			{TEXT("Dome"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					const int32 Top = Parapet(W, F, TopZ, Ids);
					const float Cx = (F.Min.X + F.Max.X) * 0.5f, Cy = (F.Min.Y + F.Max.Y) * 0.5f;
					const float R = FMath::Min(F.Max.X - F.Min.X, F.Max.Y - F.Min.Y) * 0.5f - U;
					const int32 H = FMath::Max(U, FMath::RoundToInt32(R * 0.7f));
					for (int32 Dz = 0; Dz < H; ++Dz)
					{
						const float Rz = R * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(static_cast<float>(Dz) / H)));
						const uint8 Mat = Dz < 1 ? Ids.PosterEdge : (Dz % (4 * U) == 0 ? Ids.Trim : Ids.Wall);
						for (int32 Y = FMath::FloorToInt32(Cy - Rz); Y <= FMath::CeilToInt32(Cy + Rz); ++Y)
						{
							for (int32 X = FMath::FloorToInt32(Cx - Rz); X <= FMath::CeilToInt32(Cx + Rz); ++X)
							{
								const float D2 = FMath::Square(X + 0.5f - Cx) + FMath::Square(Y + 0.5f - Cy);
								// A shell two voxels thick: the inside stays air (cheap to mesh and to break).
								if (D2 <= Rz * Rz && D2 >= FMath::Square(FMath::Max(0.f, Rz - 2.f)) - 0.01f)
								{
									W.Set(FIntVector(X, Y, TopZ + 1 + Dz), Mat);
								}
							}
						}
						if (Rz < 2.f)
						{
							W.FillBox(FIntVector(FMath::FloorToInt32(Cx) - 1, FMath::FloorToInt32(Cy) - 1, TopZ + 1 + Dz), FIntVector(FMath::FloorToInt32(Cx) + 1, FMath::FloorToInt32(Cy) + 1, TopZ + 2 + Dz), Ids.Wall);
						}
					}
					return FMath::Max(Top, Mast(W, FIntVector(FMath::FloorToInt32(Cx), FMath::FloorToInt32(Cy), TopZ + 1 + H), 5, 0, Ids.Trim, U));
				}},
			// Helipad: a low parapet, a raised slab deck with a painted cross mark and dim warm corner lights.
			{TEXT("Helipad"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					for (int32 Z = TopZ + 1; Z < TopZ + 1 + U; ++Z)
					{
						RingLayer(W, F, Z, 0, Ids.Trim);
					}
					const int32 Z = TopZ + 1 + U;
					const FIntVector Lo(F.Min.X + 2 * U, F.Min.Y + 2 * U, Z), Hi(F.Max.X - 2 * U, F.Max.Y - 2 * U, Z + U);
					if (Hi.X - Lo.X < 8 * U || Hi.Y - Lo.Y < 8 * U)
					{
						return Z;
					}
					W.FillBox(Lo, Hi, Ids.Slab);
					const FIntVector C((Lo.X + Hi.X) / 2, (Lo.Y + Hi.Y) / 2, Hi.Z - 1);
					const int32 Arm = FMath::Min(Hi.X - Lo.X, Hi.Y - Lo.Y) / 3;
					W.FillBox(FIntVector(C.X - Arm, C.Y - U / 2, C.Z), FIntVector(C.X + Arm, C.Y - U / 2 + U, C.Z + 1), Ids.Trim);
					W.FillBox(FIntVector(C.X - U / 2, C.Y - Arm, C.Z), FIntVector(C.X - U / 2 + U, C.Y + Arm, C.Z + 1), Ids.Trim);
					for (const FIntVector& Corner : {FIntVector(Lo.X, Lo.Y, Hi.Z), FIntVector(Hi.X - U, Lo.Y, Hi.Z), FIntVector(Lo.X, Hi.Y - U, Hi.Z), FIntVector(Hi.X - U, Hi.Y - U, Hi.Z)})
					{
						W.FillBox(Corner, Corner + FIntVector(U, U, U), Ids.Window);
					}
					return Hi.Z + U;
				}},
			// Shanty: a squatter town on the roof (megacity rooftops): a parapet, then a seeded jumble of one-voxel-shell
			// shacks, some stacked two high, each with a lit window strip and a trim lid.
			{TEXT("Shanty"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream& Rng)
				{
					const int32 U = Ids.Unit;
					int32 Top = Parapet(W, F, TopZ, Ids);
					const int32 SpanX = F.Max.X - F.Min.X - 4 * U, SpanY = F.Max.Y - F.Min.Y - 4 * U;
					if (SpanX < 10 * U || SpanY < 10 * U)
					{
						return Top;
					}
					const int32 Count = FMath::Clamp(SpanX * SpanY / (14 * U * 14 * U), 2, 9);
					for (int32 i = 0; i < Count; ++i)
					{
						const int32 Sx = Rng.RandRange(6, 12) * U, Sy = Rng.RandRange(6, 12) * U, Sz = Rng.RandRange(7, 10) * U;
						const int32 X0 = F.Min.X + 2 * U + Rng.RandRange(0, FMath::Max(0, SpanX - Sx));
						const int32 Y0 = F.Min.Y + 2 * U + Rng.RandRange(0, FMath::Max(0, SpanY - Sy));
						const int32 Levels = Rng.FRand() < 0.35f ? 2 : 1;
						for (int32 L = 0; L < Levels; ++L)
						{
							const int32 In = L * 2 * U; // the upper shack sits a little in from the lower one
							const FIntVector Lo(X0 + In, Y0 + In, TopZ + 1 + L * (Sz + U)), Hi(X0 + Sx - In, Y0 + Sy - In, Lo.Z + Sz);
							if (Hi.X - Lo.X < 4 * U || Hi.Y - Lo.Y < 4 * U)
							{
								break;
							}
							W.FillBox(Lo, Hi, Ids.Wall);
							W.FillBox(Lo + FIntVector(1, 1, 0), Hi - FIntVector(1, 1, 1), 0);
							W.FillBox(FIntVector(Lo.X + U, Lo.Y, Lo.Z + Sz / 2), FIntVector(Hi.X - U, Lo.Y + 1, Lo.Z + Sz / 2 + U), (i + L) % 3 == 0 ? Ids.WindowAlt : Ids.Window);
							W.FillBox(FIntVector(Lo.X - U / 2, Lo.Y - U / 2, Hi.Z), FIntVector(Hi.X + U / 2, Hi.Y + U / 2, Hi.Z + U / 2 + 1), Ids.Trim);
							Top = FMath::Max(Top, Hi.Z + U / 2 + 1);
						}
					}
					return Top;
				}},
			// Sawtooth: an industrial roof of north-light ridges across X, the steep side glazed (the alternate window).
			{TEXT("Sawtooth"), [](FDCVoxelWorld& W, const FFaceFrame& F, int32 TopZ, const FCraftIds& Ids, FRandomStream&)
				{
					const int32 U = Ids.Unit;
					const int32 Pitch = 6 * U;
					const int32 Rise = 4 * U;
					for (int32 X0 = F.Min.X; X0 + Pitch <= F.Max.X; X0 += Pitch)
					{
						// Skins only: the sloped deck, the glazed steep face, and the two gable ends.
						for (int32 Dx = 0; Dx < Pitch; ++Dx)
						{
							const int32 H = FMath::Max(1, Rise * (Dx + 1) / Pitch);
							W.FillBox(FIntVector(X0 + Dx, F.Min.Y, TopZ + H), FIntVector(X0 + Dx + 1, F.Max.Y, TopZ + 1 + H), Ids.Wall);
							W.FillBox(FIntVector(X0 + Dx, F.Min.Y, TopZ + 1), FIntVector(X0 + Dx + 1, F.Min.Y + 1, TopZ + 1 + H), Ids.Wall);
							W.FillBox(FIntVector(X0 + Dx, F.Max.Y - 1, TopZ + 1), FIntVector(X0 + Dx + 1, F.Max.Y, TopZ + 1 + H), Ids.Wall);
						}
						W.FillBox(FIntVector(X0 + Pitch - 1, F.Min.Y + 1, TopZ + 1), FIntVector(X0 + Pitch, F.Max.Y - 1, TopZ + Rise), Ids.WindowAlt);
					}
					return TopZ + 1 + Rise;
				}},
		};
		return Table;
	}

	/** A sign's extent on a face: along-face U [U0, U1), height Z [Z0, Z1), depth 1..Depth out from the wall. */
	struct FSignShape
	{
		int32 U0 = 0, U1 = 0, Z0 = 0, Z1 = 0, Depth = 1;
		bool IsValid() const { return U1 > U0 && Z1 > Z0 && Depth > 0; }
	};

	/** Sign registry (critic V19.4: storefront-scale neon). Each builder sizes a sign on one face from the recipe. */
	using FSignFn = FSignShape (*)(const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream& Rng);
	const TMap<FName, FSignFn>& Signs()
	{
		static const TMap<FName, FSignFn> Table = {
			// A vertical blade standing out from a pier between two bays, from the first floor up.
			{TEXT("Blade"), [](const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream& Rng)
				{
					const int32 Bays = F.Length(Face) / B;
					FSignShape S;
					if (Bays < 2 || Floors < 3)
					{
						return S;
					}
					S.U0 = Rng.RandRange(1, Bays - 1) * B - R.SignWidthVox / 2;
					S.U1 = S.U0 + FMath::Max(1, R.SignWidthVox);
					S.Z0 = B + Rng.RandRange(0, B / 2);
					S.Z1 = S.Z0 + FMath::Min(R.SignHeightVox, (Floors - 1) * B - S.Z0);
					S.Depth = FMath::Max(2, R.SignDepthVox);
					return S;
				}},
			// A horizontal band over a ground-floor bay, under the first string course.
			{TEXT("Board"), [](const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream& Rng)
				{
					const int32 Bays = F.Length(Face) / B;
					FSignShape S;
					if (Bays < 1)
					{
						return S;
					}
					const int32 W = FMath::Min(R.SignWidthVox, B - 4);
					S.U0 = Rng.RandRange(0, Bays - 1) * B + (B - W) / 2;
					S.U1 = S.U0 + W;
					S.Z0 = B * 2 / 3 + 1;
					S.Z1 = S.Z0 + FMath::Min(R.SignHeightVox, B - S.Z0 - 1);
					S.Depth = FMath::Max(1, R.SignDepthVox);
					return S;
				}},
			// R23: a cloth banner hanging from under the roof line down the face (tall, flat, one or two voxels proud).
			{TEXT("Banner"), [](const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream& Rng)
				{
					const int32 Bays = F.Length(Face) / B;
					FSignShape S;
					if (Bays < 2 || Floors < 4)
					{
						return S;
					}
					const int32 W = FMath::Clamp(R.SignWidthVox, 3, B - 2);
					S.U0 = Rng.RandRange(0, Bays - 1) * B + (B - W) / 2;
					S.U1 = S.U0 + W;
					S.Z1 = Floors * B - B / 4;
					S.Z0 = FMath::Max(B, S.Z1 - R.SignHeightVox);
					S.Depth = FMath::Clamp(R.SignDepthVox, 1, 2);
					return S;
				}},
			// R23: a corporate crown band across the top floor of a face, the tower's name as glyph strokes.
			{TEXT("Crown"), [](const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream&)
				{
					const int32 Len = F.Length(Face);
					FSignShape S;
					if (Len < 3 * B || Floors < 3)
					{
						return S;
					}
					S.U0 = B / 2;
					S.U1 = Len - B / 2;
					S.Z0 = (Floors - 1) * B + B / 4;
					S.Z1 = S.Z0 + FMath::Clamp(R.SignHeightVox, 3, B * 3 / 4);
					S.Depth = FMath::Clamp(R.SignDepthVox, 1, 3);
					return S;
				}},
			// The first run's two-bay board (kept for recipes that still name it).
			{TEXT("Poster"), [](const FFaceFrame& F, int32 Face, int32 B, int32 Floors, const FDCVoxelRecipe& R, FRandomStream& Rng)
				{
					const int32 Len = F.Length(Face);
					const int32 Width = FMath::Min(B * 2 - 2, Len - 4);
					FSignShape S;
					if (Width < 6 || Floors < 4)
					{
						return S;
					}
					S.U0 = Rng.RandRange(2, FMath::Max(2, Len - Width - 2));
					S.U1 = S.U0 + Width;
					S.Z0 = B + 2;
					S.Z1 = S.Z0 + FMath::Min(B * 2 - 2, (Floors - 2) * B);
					S.Depth = 1;
					return S;
				}},
		};
		return Table;
	}

	void PaintSign(FDCVoxelWorld& W, const FFaceFrame& F, int32 Face, const FSignShape& S, int32 GlyphEvery, const FCraftIds& Ids, TArray<FDCVoxelSign>& OutSigns)
	{
		// The face a passer-by reads: a blade (deeper than wide) shows its depth-height side, a board its front.
		const bool bBlade = S.Depth > S.U1 - S.U0;
		const int32 Long = bBlade ? S.Z1 - S.Z0 : FMath::Max(S.U1 - S.U0, S.Z1 - S.Z0);
		const bool bGlyphRows = bBlade || S.Z1 - S.Z0 > S.U1 - S.U0;
		const int32 G = GlyphEvery > 1 && Long > 2 * GlyphEvery ? GlyphEvery : 0;
		FIntVector Lo(MAX_int32, MAX_int32, MAX_int32);
		FIntVector Hi(MIN_int32, MIN_int32, MIN_int32);
		for (int32 Dp = 1; Dp <= S.Depth; ++Dp)
		{
			for (int32 U = S.U0; U < S.U1; ++U)
			{
				for (int32 Z = S.Z0; Z < S.Z1; ++Z)
				{
					const bool bEdgeZ = Z == S.Z0 || Z == S.Z1 - 1;
					const bool bEdge = bEdgeZ || (bBlade ? Dp == S.Depth : (U == S.U0 || U == S.U1 - 1));
					// Dark glyph strokes across the long axis, a lit margin left round them: reads as lettering.
					const int32 Along = bGlyphRows ? Z - S.Z0 : U - S.U0;
					const bool bInner = bBlade ? (Dp >= 2 && Dp <= S.Depth - 2) : (bGlyphRows ? (U > S.U0 + 1 && U < S.U1 - 2) : (Z > S.Z0 + 1 && Z < S.Z1 - 2));
					const bool bGlyph = G > 0 && !bEdge && bInner && Along % G == G / 2;
					const FIntVector P = F.Cell(Face, -Dp, U, Z);
					W.Set(P, bEdge ? Ids.PosterEdge : (bGlyph ? Ids.Ink : Ids.Poster));
					Lo = Lo.ComponentMin(P);
					Hi = Hi.ComponentMax(P);
				}
			}
		}
		FDCVoxelSign& Sign = OutSigns.AddDefaulted_GetRef();
		const int32 Zm = (S.Z0 + S.Z1) / 2;
		Sign.Center = F.Cell(Face, -S.Depth, (S.U0 + S.U1) / 2, Zm);
		Sign.Probe = F.Cell(Face, -S.Depth, S.U0, Zm); // an edge voxel (blade tip or board side): always lit
		Sign.Normal = F.Cell(Face, -1, 0, 0) - F.Cell(Face, 0, 0, 0);
		Sign.WidthVox = FMath::Max(S.U1 - S.U0, S.Depth);
		Sign.HeightVox = S.Z1 - S.Z0;
		Sign.Material = Ids.PosterEdge;
		Sign.BoxMin = Lo;
		Sign.BoxMax = Hi + FIntVector(1, 1, 1);
		const FIntVector B0 = F.Cell(Face, 0, S.U0, S.Z0);
		const FIntVector B1 = F.Cell(Face, 0, S.U1 - 1, S.Z1 - 1);
		Sign.BackMin = B0.ComponentMin(B1);
		Sign.BackMax = B0.ComponentMax(B1) + FIntVector(1, 1, 1);
		Sign.BackCells = (S.U1 - S.U0) * (S.Z1 - S.Z0);
	}
	/** What a mount needs to place a building attachment (R2 props). */
	struct FMountCtx
	{
		FDCVoxelWorld& World;
		/** The frame of every floor (setbacks applied) and the roof slab's z. */
		const TArray<FFaceFrame>& FloorFrames;
		int32 RoofZ = 0;
		int32 Bay = 1;
		int32 Vcm = 10;
		int32 Unit = 1;
		const FDCVoxelCraftContext& Ctx;
	};

	/** Outward face (-X, +X, -Y, +Y) -> quarter turns that point a model's +x out of it. */
	constexpr int32 MountFaceTurns[4] = {2, 0, 3, 1};

	/** A model's size in tower voxels (x forward, y across, z up). */
	FIntVector ModelVox(const FDCVoxelModel& M, int32 Vcm)
	{
		return FIntVector(FMath::Max(1, M.Size.X * M.GridCm / Vcm), FMath::Max(1, M.Size.Y * M.GridCm / Vcm), FMath::Max(1, M.Size.Z * M.GridCm / Vcm));
	}

	/** Mount registry (R2 props, rule 3): mount id -> how an attachment sits on its tower, and how far it reaches out of
	 *  the footprint (the tower grid's margin grows to hold it). Stamp returns the top z written + 1 (0 = skipped). */
	struct FMount
	{
		/** Base receives the pivot cell (grid-local) and Turns the quarter turns, for the attachment's light spot. */
		int32 (*Stamp)(const FMountCtx& C, const FDCVoxelModel& M, const FDCAttachPlan& A, FIntVector& Base, int32& Turns);
		int32 (*Reach)(const FDCVoxelModel& M, int32 Vcm);
	};
	const TMap<FName, FMount>& Mounts()
	{
		static const TMap<FName, FMount> Table = {
			// On the roof deck inside the parapet, never over what the roof builder put there (stair hut, mast).
			{TEXT("Roof"), {[](const FMountCtx& C, const FDCVoxelModel& M, const FDCAttachPlan& A, FIntVector& Base, int32& Turns)
				{
					const FFaceFrame& F = C.FloorFrames.Last();
					const FIntVector S = ModelVox(M, C.Vcm);
					const bool bSide = (A.Face & 1) != 0;
					const int32 Hx = (bSide ? S.Y : S.X) / 2 + C.Unit + 1;
					const int32 Hy = (bSide ? S.X : S.Y) / 2 + C.Unit + 1;
					const int32 X0 = F.Min.X + Hx, X1 = F.Max.X - Hx;
					const int32 Y0 = F.Min.Y + Hy, Y1 = F.Max.Y - Hy;
					if (X1 < X0 || Y1 < Y0)
					{
						return 0; // the deck is too small for it
					}
					Base = FIntVector(X0 + FMath::RoundToInt32(A.U * (X1 - X0)), Y0 + FMath::RoundToInt32(A.V * (Y1 - Y0)), C.RoofZ + 1);
					Turns = A.Face;
					return DCVoxelCraft::StampModel(C.World, M, C.Vcm, Base, Turns, C.Ctx, true);
				},
				[](const FDCVoxelModel&, int32) { return 0; }}},
			// At the deck edge of face Face, inside the parapet, facing out over the street (rooftop billboards).
			{TEXT("RoofEdge"), {[](const FMountCtx& C, const FDCVoxelModel& M, const FDCAttachPlan& A, FIntVector& Base, int32& Turns)
				{
					const FFaceFrame& F = C.FloorFrames.Last();
					const FIntVector S = ModelVox(M, C.Vcm);
					const int32 Face = ((A.Face % 4) + 4) % 4;
					const int32 Len = F.Length(Face);
					const int32 Half = S.Y / 2 + 1;
					const int32 Depth = C.Unit + 1 + S.X / 2;
					if (Len < 2 * Half + 2 || F.Length(Face ^ 2) < 2 * Depth + 2)
					{
						return 0;
					}
					const int32 U = FMath::Clamp(FMath::RoundToInt32(A.U * Len), Half, Len - Half);
					Base = F.Cell(Face, Depth, U, C.RoofZ + 1);
					Turns = MountFaceTurns[Face];
					return DCVoxelCraft::StampModel(C.World, M, C.Vcm, Base, Turns, C.Ctx, true);
				},
				[](const FDCVoxelModel&, int32) { return 0; }}},
			// On a facade at V floors up, back against the wall (model x = 0 one voxel proud), along the face at U.
			{TEXT("Wall"), {[](const FMountCtx& C, const FDCVoxelModel& M, const FDCAttachPlan& A, FIntVector& Base, int32& Turns)
				{
					const int32 Floors = C.FloorFrames.Num();
					// Kept under the roof line: a sign rolled for floor 4 on a three-floor block sits at its top floor.
					const int32 Z = FMath::Clamp(FMath::RoundToInt32(A.V * C.Bay), 1, FMath::Max(1, Floors * C.Bay - ModelVox(M, C.Vcm).Z - 1));
					const FFaceFrame& F = C.FloorFrames[FMath::Clamp(Z / C.Bay, 0, Floors - 1)];
					const int32 Face = ((A.Face % 4) + 4) % 4;
					const int32 Len = F.Length(Face);
					const int32 Half = ModelVox(M, C.Vcm).Y / 2 + 1;
					if (Len < 2 * Half + 2)
					{
						return 0;
					}
					const int32 U = FMath::Clamp(FMath::RoundToInt32(A.U * Len), Half, Len - Half);
					Base = F.Cell(Face, -1, U, Z);
					Turns = MountFaceTurns[Face];
					return DCVoxelCraft::StampModel(C.World, M, C.Vcm, Base, Turns, C.Ctx, true);
				},
				[](const FDCVoxelModel& M, int32 Vcm) { return ModelVox(M, Vcm).X + 1; }}},
		};
		return Table;
	}
}

namespace DCVoxelCraft
{
	void ApplyRecipes(FDCCityPlan& Plan, const FDCVoxelSectorLayout& Layout, const TArray<FDCVoxelRecipe>& Recipes, int32 MaxFitBays)
	{
		if (Recipes.Num() == 0)
		{
			return;
		}
		TArray<TPair<int32, float>> Weighted;
		float Total = 0.f;
		for (const FDCVoxelRecipeWeight& W : Layout.Recipes)
		{
			const int32 Index = Recipes.IndexOfByPredicate([&W](const FDCVoxelRecipe& R) { return R.Id == W.Recipe; });
			if (Index != INDEX_NONE && W.Weight > 0.f)
			{
				Weighted.Add({Index, W.Weight});
				Total += W.Weight;
			}
		}
		if (Weighted.Num() == 0)
		{
			Weighted.Add({0, 1.f});
			Total = 1.f;
		}
		for (FDCLotPlan& Lot : Plan.Buildings)
		{
			FRandomStream Rng(Lot.Spec.Seed ^ 0x7EC1BE);
			float Pick = Rng.FRand() * Total;
			int32 Chosen = Weighted.Last().Key;
			for (const TPair<int32, float>& W : Weighted)
			{
				if (Pick < W.Value)
				{
					Chosen = W.Key;
					break;
				}
				Pick -= W.Value;
			}
			const FDCVoxelRecipe& R = Recipes[Chosen];
			Lot.Spec.Recipe = static_cast<uint8>(Chosen);
			const int32 Fit = FMath::Max(2, MaxFitBays);
			Lot.Spec.SizeX = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, Fit);
			Lot.Spec.SizeY = FMath::Clamp(Rng.RandRange(R.MinBays, R.MaxBays), 2, Fit);
			Lot.Spec.Floors = FMath::Max(2, Rng.RandRange(R.MinFloors, R.MaxFloors));
		}
	}

	bool IsRoofKind(FName Roof) { return Roofs().Contains(Roof); }
	bool IsSignKind(FName Sign) { return Signs().Contains(Sign); }
	bool IsBayKind(TCHAR Bay) { return Bays().Contains(Bay); }

	void CraftTower(FDCVoxelTower& Tower, const FDCLotPlan& Lot, int32 LotIndex, const FDCVoxelCraftContext& Ctx)
	{
		Tower.Lot = LotIndex;
		const FDCVoxelRecipe* R = Ctx.Recipe(Lot.Spec.Recipe);
		if (!R || !Ctx.Tuning)
		{
			return;
		}
		const int32 Vcm = FMath::Max(1, Ctx.Tuning->TowerVoxelCm);
		const int32 B = FMath::Max(6, Ctx.BayCm / Vcm);
		int32 Margin = FMath::Max3(3, 60 / Vcm, R->SignDepthVox + 2); // room for trim, signs and the bite's outer edge
		for (const FDCAttachPlan& A : Lot.Attach) // and for wall mounts standing out of the facade (R2 props)
		{
			const FDCVoxelModel* const* M = Ctx.Models.Find(A.Model);
			const FMount* Mount = Mounts().Find(A.Mount);
			Margin = (M && *M && Mount) ? FMath::Max(Margin, Mount->Reach(**M, Vcm) + 2) : Margin;
		}
		Tower.Grid.Data.Reset();
		Tower.Signs.Reset();
		Tower.AttachSpots.Reset();
		Tower.Grid.VoxelCm = Vcm;
		const FIntVector Center = DCVoxelMath::ToCm(Lot.Location);
		const FIntVector Foot(Lot.Spec.SizeX * B, Lot.Spec.SizeY * B, Lot.Spec.Floors * B);
		Tower.Grid.OriginCm = FIntVector(Center.X - Foot.X * Vcm / 2 - Margin * Vcm, Center.Y - Foot.Y * Vcm / 2 - Margin * Vcm, 0);
		Tower.Min = FIntVector(Margin, Margin, 0);
		Tower.Max = Tower.Min + Foot;
		FDCVoxelWorld& World = Tower.Grid.Data;

		FCraftIds Ids;
		Ids.Wall = Ctx.MaterialId(R->Wall);
		Ids.Window = Ctx.MaterialId(R->Window);
		Ids.WindowOff = R->WindowOff.IsNone() ? Ids.Window : Ctx.MaterialId(R->WindowOff);
		Ids.WindowAlt = R->WindowAlt.IsNone() ? Ids.Window : Ctx.MaterialId(R->WindowAlt);
		Ids.Trim = Ctx.MaterialId(R->Trim);
		Ids.Slab = Ctx.MaterialId(R->Slab);
		Ids.Poster = Ctx.MaterialId(R->Poster);
		Ids.PosterEdge = R->PosterEdge.IsNone() ? Ids.Poster : Ctx.MaterialId(R->PosterEdge);
		Ids.Ink = Ctx.MaterialId(TEXT("Ink"));
		Ids.Unit = FMath::Max(1, 25 / Vcm);
		FRandomStream Rng(Lot.Spec.Seed ^ 0xC2AF7);
		const int32 Wall = FMath::Clamp(R->WallCm / Vcm, 1, B / 3);
		const int32 Sill = R->WindowSillCm / Vcm;
		const int32 Win = FMath::Max(1, R->WindowHeightCm / Vcm);
		const int32 Setback = R->SetbackCm / Vcm;
		const FString Upper = R->Facade.IsEmpty() ? FString(TEXT("W")) : R->Facade;
		const FString Ground = R->Base.IsEmpty() ? Upper : R->Base;
		const FBayFn Solid = Bays()[TEXT('P')];
		const int32 Rib = R->PilasterCm > 0 ? FMath::Max(1, R->PilasterCm / Vcm) : 0;
		const int32 Lintel = R->LintelCm > 0 ? FMath::Max(1, R->LintelCm / Vcm) : 0;

		FFaceFrame Frame{Tower.Min, Tower.Max};
		TArray<FFaceFrame> FloorFrames;
		int32 Top = 0;
		for (int32 Floor = 0; Floor < Lot.Spec.Floors; ++Floor)
		{
			const int32 Inset = R->SetbackEvery > 0 ? (Floor / R->SetbackEvery) * Setback : 0;
			const FIntVector In(Inset, Inset, 0);
			FFaceFrame F{Tower.Min + In, FIntVector(Tower.Max.X, Tower.Max.Y, 0) - In};
			if (F.Max.X - F.Min.X < B * 2 || F.Max.Y - F.Min.Y < B * 2)
			{
				F = Frame; // never step in past two bays
			}
			Frame = F;
			FloorFrames.Add(F);
			const int32 Z0 = Floor * B;
			// Floor slab (structure), then the walls of this floor.
			World.FillBox(FIntVector(F.Min.X, F.Min.Y, Z0), FIntVector(F.Max.X, F.Max.Y, Z0 + 1), Ids.Slab);
			const FString& Pattern = Floor == 0 ? Ground : Upper;
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const int32 Len = F.Length(Face);
				for (int32 U = 0; U < Len; ++U)
				{
					const bool bCorner = U < Wall || U >= Len - Wall;
					const FBayFn* Fn = Bays().Find(Pattern[(U / B) % Pattern.Len()]);
					const FBayFn Bay = Fn ? *Fn : Solid;
					for (int32 H = 0; H < B; ++H)
					{
						const EOpening Open = bCorner || H == 0 ? EOpening::Solid : Bay(U % B, H, B, Sill, Win);
						for (int32 D = 0; D < Wall; ++D)
						{
							const uint8 Outer = Open == EOpening::Solid ? Ids.Wall : 0;
							uint8 Glass = Ids.Window;
							if (Open == EOpening::Window)
							{
								// One pick per opening (bay, floor, face, lot): dark, the alternate tint, or the recipe's.
								const uint32 Pick = DCVoxelOps::Hash(FIntVector(U / B, Floor, Face), static_cast<uint32>(LotIndex) * 2654435761u) % 100u;
								Glass = static_cast<int32>(Pick) < R->WindowOffPercent ? Ids.WindowOff : (static_cast<int32>(Pick) < R->WindowOffPercent + R->WindowAltPercent ? Ids.WindowAlt : Ids.Window);
							}
							const uint8 Inner = Open == EOpening::Window ? Glass : Outer;
							World.Set(F.Cell(Face, D, U, Z0 + H), D == 0 ? Outer : Inner);
						}
						// Window sill: a trim ledge one voxel proud under each opening.
						if (!bCorner && H > 0 && H + 1 == Sill && Bay(U % B, Sill, B, Sill, Win) == EOpening::Window)
						{
							World.Set(F.Cell(Face, -1, U, Z0 + H), Ids.Trim);
						}
						// Lintel: a proud cap over each opening (relief, V5.3).
						if (!bCorner && Lintel > 0 && H >= Sill + Win && H < Sill + Win + Lintel && Bay(U % B, Sill, B, Sill, Win) == EOpening::Window)
						{
							World.Set(F.Cell(Face, -1, U, Z0 + H), Ids.Wall);
						}
						// Pier ribs at each bay start and the corners stand proud (RibDepthVox), floor to floor.
						if (Rib > 0 && (bCorner || U % B < Rib))
						{
							for (int32 Dp = 1; Dp <= FMath::Clamp(R->RibDepthVox, 1, 4); ++Dp)
							{
								World.Set(F.Cell(Face, -Dp, U, Z0 + H), Ids.Wall);
							}
						}
					}
				}
			}
			if (Floor == 0 || (R->BandEvery > 0 && Floor % R->BandEvery == 0))
			{
				for (int32 Z = Z0; Z < Z0 + Ids.Unit; ++Z)
				{
					for (int32 Dp = 1; Dp <= FMath::Clamp(R->LedgeDepthVox, 1, 4); ++Dp)
					{
						RingLayer(World, F, Z, -Dp, Ids.Trim, Dp); // plinth and string courses stand proud of the wall
					}
				}
			}
			Top = Z0 + B;
		}
		// Roof slab, then the roof builder.
		World.FillBox(FIntVector(Frame.Min.X, Frame.Min.Y, Top), FIntVector(Frame.Max.X, Frame.Max.Y, Top + 1), Ids.Slab);
		RingLayer(World, Frame, Top, 0, Ids.Wall);
		const FRoofFn* Roof = Roofs().Find(R->Roof);
		int32 RoofTop = (Roof ? *Roof : Roofs()[TEXT("Flat")])(World, Frame, Top, Ids, Rng);

		// Roof clutter and wall mounts (R2 props) in the tower's own grid: they fall and shatter with it.
		if (FloorFrames.Num() > 0)
		{
			const FMountCtx MountCtx{World, FloorFrames, Top, B, Vcm, Ids.Unit, Ctx};
			for (const FDCAttachPlan& A : Lot.Attach)
			{
				const FDCVoxelModel* const* M = Ctx.Models.Find(A.Model);
				const FMount* Mount = Mounts().Find(A.Mount);
				if (!M || !*M || !Mount)
				{
					continue;
				}
				FIntVector Base = FIntVector::ZeroValue;
				int32 Turns = 0;
				const int32 StampTop = Mount->Stamp(MountCtx, **M, A, Base, Turns);
				if (StampTop <= 0)
				{
					continue; // did not fit
				}
				RoofTop = FMath::Max(RoofTop, StampTop);
				// Light spot: the model's box centre, from its pivot along the way it faces (+x turned by Turns).
				static const FIntVector Facing[4] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
				const FIntVector N = Facing[((Turns % 4) + 4) % 4];
				const FIntVector S = ModelVox(**M, Vcm);
				const int32 Forward = S.X / 2 - (*M)->Pivot.X * (*M)->GridCm / Vcm;
				FDCVoxelAttachSpot& Spot = Tower.AttachSpots.AddDefaulted_GetRef();
				Spot.Model = A.Model;
				Spot.Normal = N;
				Spot.CenterCm = Tower.Grid.CenterCm(Base + N * Forward + FIntVector(0, 0, S.Z / 2));
			}
		}

		const FFaceFrame Full{Tower.Min, Tower.Max};
		for (int32 Face = 0; Face < 4; ++Face)
		{
			if (Rng.FRand() < R->PosterChance)
			{
				const FSignFn* Sign = Signs().Find(R->SignKind);
				const FSignShape Shape = Sign ? (*Sign)(Full, Face, B, Lot.Spec.Floors, *R, Rng) : FSignShape();
				if (Shape.IsValid())
				{
					PaintSign(World, Full, Face, Shape, R->SignGlyphEvery, Ids, Tower.Signs);
				}
			}
		}

		Tower.ScanMin = FIntVector(Tower.Min.X - Margin + 1, Tower.Min.Y - Margin + 1, 0);
		Tower.ScanMax = FIntVector(Tower.Max.X + Margin - 1, Tower.Max.Y + Margin - 1, FMath::Max(RoofTop, Top + 1) + 1);
		Tower.IntactLayers.SetNumZeroed(Tower.ScanMax.Z);
		for (int32 Z = 0; Z < Tower.ScanMax.Z; ++Z)
		{
			Tower.IntactLayers[Z] = CountLayer(World, Tower.ScanMin, Tower.ScanMax, Z);
		}
		Tower.bFallen = false;
		Tower.BreakZ = 0;
	}

	void CraftTerrain(FDCVoxelGrid& Out, const FDCCityPlan& Plan, float LotSizeCm, const FDCVoxelCraftContext& Ctx)
	{
		const FDCVoxelTuning& T = *Ctx.Tuning;
		const int32 Vcm = FMath::Max(1, T.TerrainVoxelCm);
		const int32 Layers = FMath::Max(1, T.GroundLayers);
		const int32 Half = FMath::RoundToInt32(Plan.HalfExtent) + T.TerrainMarginCm;
		const int32 N = 2 * Half / Vcm;
		Out.Data.Reset();
		Out.VoxelCm = Vcm;
		Out.OriginCm = FIntVector(-Half, -Half, -Layers * Vcm);
		const uint8 Street = Ctx.MaterialId(T.StreetMaterial);
		const uint8 Paving = Ctx.MaterialId(T.LotMaterial);
		const uint8 Kerb = Ctx.MaterialId(T.KerbMaterial);
		Out.Data.FillBox(FIntVector(0, 0, 0), FIntVector(N, N, Layers), Street);
		// Lane dashes down the middle of every street (flush paint in the top layer).
		const uint8 Paint = Ctx.MaterialId(TEXT("Paint"));
		TArray<int32> LinesX, LinesY;
		for (const FVector& P : Plan.StreetPoints)
		{
			LinesX.AddUnique(FMath::RoundToInt32(P.X));
			LinesY.AddUnique(FMath::RoundToInt32(P.Y));
		}
		const int32 DashOn = FMath::Max(1, 150 / Vcm);
		const int32 DashPeriod = FMath::Max(DashOn + 1, 450 / Vcm);
		for (int32 X : LinesX)
		{
			const int32 Lx = Out.ToLocal(FIntVector(X, 0, 0)).X;
			for (int32 Y = 0; Y < N; ++Y)
			{
				if (Y % DashPeriod < DashOn)
				{
					Out.Data.Set(FIntVector(Lx, Y, Layers - 1), Paint);
				}
			}
		}
		for (int32 Y : LinesY)
		{
			const int32 Ly = Out.ToLocal(FIntVector(0, Y, 0)).Y;
			for (int32 X = 0; X < N; ++X)
			{
				if (X % DashPeriod < DashOn)
				{
					Out.Data.Set(FIntVector(X, Ly, Layers - 1), Paint);
				}
			}
		}
		// Every built lot and plaza gets paving and a kerb ring one voxel above the street.
		const int32 LotHalf = FMath::RoundToInt32((Plan.LotSizeCm > 0.f ? Plan.LotSizeCm : LotSizeCm) * 0.5f);
		const int32 Z = Layers - 1;
		TArray<FVector> Paved;
		for (const FDCLotPlan& Lot : Plan.Buildings)
		{
			Paved.Add(Lot.Location);
		}
		Paved.Append(Plan.Plazas);
		Paved.Append(Plan.RubbleZones);
		for (const FVector& Where : Paved)
		{
			const FIntVector C = DCVoxelMath::ToCm(Where);
			const FIntVector Lo = Out.ToLocal(FIntVector(C.X - LotHalf, C.Y - LotHalf, 0));
			const FIntVector Hi = Out.ToLocal(FIntVector(C.X + LotHalf, C.Y + LotHalf, 0));
			Out.Data.FillBox(FIntVector(Lo.X, Lo.Y, Z), FIntVector(Hi.X, Hi.Y, Z + 1), Paving);
			for (int32 X = Lo.X; X < Hi.X; ++X)
			{
				Out.Data.Set(FIntVector(X, Lo.Y, Z + 1), Kerb);
				Out.Data.Set(FIntVector(X, Hi.Y - 1, Z + 1), Kerb);
			}
			for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
			{
				Out.Data.Set(FIntVector(Lo.X, Y, Z + 1), Kerb);
				Out.Data.Set(FIntVector(Hi.X - 1, Y, Z + 1), Kerb);
			}
		}
		// R19: the sector's edge. A barrier ring on the bounds line, rubble heaps in the band outside it (seeded from
		// the cell, the same on every machine): the street visibly ends before the terrain does.
		const int32 Ring = FMath::Clamp((FMath::RoundToInt32(Plan.HalfExtent) + T.BoundsMarginCm) / Vcm, 1, N / 2 - 1);
		const int32 Lo = N / 2 - Ring, Hi = N / 2 + Ring;
		const uint8 Barrier = Ctx.MaterialId(T.BarrierMaterial);
		const uint8 Rubble = Ctx.MaterialId(TEXT("Rubble"));
		const int32 BarrierCells = FMath::Max(1, T.BarrierHeightCm / Vcm);
		const int32 RubbleCells = FMath::Max(1, T.EdgeRubbleMaxCm / Vcm);
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const bool bRing = ((X == Lo || X == Hi) && Y >= Lo && Y <= Hi) || ((Y == Lo || Y == Hi) && X >= Lo && X <= Hi);
				const bool bOutside = X < Lo || X > Hi || Y < Lo || Y > Hi;
				if (bRing)
				{
					Out.Data.FillBox(FIntVector(X, Y, Layers), FIntVector(X + 1, Y + 1, Layers + BarrierCells), Barrier);
					continue;
				}
				const uint32 H = HashCombine(GetTypeHash(X * 73856093), GetTypeHash(Y * 19349663)) % 10000u;
				if (bOutside && H < static_cast<uint32>(T.EdgeRubbleChance * 10000.f))
				{
					Out.Data.FillBox(FIntVector(X, Y, Layers), FIntVector(X + 1, Y + 1, Layers + 1 + static_cast<int32>(H % RubbleCells)), Rubble);
				}
			}
		}
	}

	void CraftRubbleZones(FDCVoxelGrid& Out, const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx)
	{
		// A pre-ruined lot: a mound of rubble, highest at the centre, ragged from the hash (the war got here first).
		const int32 V = Out.VoxelCm;
		const int32 Half = FMath::RoundToInt32(Plan.LotSizeCm * 0.45f);
		const uint8 Rubble = Ctx.MaterialId(TEXT("Rubble"));
		const uint8 Char = Ctx.MaterialId(TEXT("Char"));
		const uint8 InkId = Ctx.MaterialId(TEXT("Ink"));
		const int32 Base = FMath::Max(1, Ctx.Tuning->GroundLayers);
		for (const FVector& Where : Plan.RubbleZones)
		{
			const FIntVector C = DCVoxelMath::ToCm(Where);
			const FIntVector Lo = Out.ToLocal(FIntVector(C.X - Half, C.Y - Half, 0));
			const FIntVector Hi = Out.ToLocal(FIntVector(C.X + Half, C.Y + Half, 0));
			const FIntVector Mid = (Lo + Hi) / 2;
			const int32 R = FMath::Max(1, (Hi.X - Lo.X) / 2);
			for (int32 Y = Lo.Y; Y < Hi.Y; ++Y)
			{
				for (int32 X = Lo.X; X < Hi.X; ++X)
				{
					const float D = FMath::Sqrt(static_cast<float>((X - Mid.X) * (X - Mid.X) + (Y - Mid.Y) * (Y - Mid.Y))) / R;
					const uint32 H = DCPcg::Hash(static_cast<int32>(X * 7919 + Y), 0xD0B8u, 0);
					const int32 Height = FMath::Max(0, FMath::RoundToInt32((1.f - D) * 7.f) + static_cast<int32>(H % 3) - 1);
					for (int32 k = 0; k < Height; ++k)
					{
						const uint32 Hm = H >> (k + 3);
						Out.Data.Set(FIntVector(X, Y, Base + k), (Hm % 5) == 0 ? Char : ((Hm % 7) == 0 ? InkId : Rubble));
					}
				}
			}
		}
	}

	void CraftCity(const FDCCityPlan& Plan, const FDCVoxelCraftContext& Ctx, TArray<FDCVoxelTower>& OutTowers)
	{
		OutTowers.Reset();
		OutTowers.SetNum(Plan.Buildings.Num());
		// Towers are independent grids: craft them in parallel (each task writes only its own grid).
		ParallelFor(Plan.Buildings.Num(), [&](int32 Lot) { CraftTower(OutTowers[Lot], Plan.Buildings[Lot], Lot, Ctx); });
	}

	int32 CountLayer(const FDCVoxelWorld& World, const FIntVector& Min, const FIntVector& Max, int32 Z)
	{
		// Chunk-wise: one map lookup per chunk instead of per voxel.
		int32 N = 0;
		const int32 S = FDCVoxelChunk::Size;
		const FIntVector CMin = FDCVoxelWorld::ChunkOf(FIntVector(Min.X, Min.Y, Z));
		const FIntVector CMax = FDCVoxelWorld::ChunkOf(FIntVector(Max.X - 1, Max.Y - 1, Z));
		const int32 Lz = Z & FDCVoxelChunk::Mask;
		for (int32 Cy = CMin.Y; Cy <= CMax.Y; ++Cy)
		{
			for (int32 Cx = CMin.X; Cx <= CMax.X; ++Cx)
			{
				const FDCVoxelChunk* C = World.FindChunk(FIntVector(Cx, Cy, CMin.Z));
				if (!C || C->Solid == 0)
				{
					continue;
				}
				const int32 X0 = FMath::Max(Min.X, Cx * S) - Cx * S;
				const int32 X1 = FMath::Min(Max.X, Cx * S + S) - Cx * S;
				const int32 Y0 = FMath::Max(Min.Y, Cy * S) - Cy * S;
				const int32 Y1 = FMath::Min(Max.Y, Cy * S + S) - Cy * S;
				for (int32 Y = Y0; Y < Y1; ++Y)
				{
					for (int32 X = X0; X < X1; ++X)
					{
						N += C->V[FDCVoxelChunk::Index(X, Y, Lz)] != 0 ? 1 : 0;
					}
				}
			}
		}
		return N;
	}
}
