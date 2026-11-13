#include "Rules/DCPcg.h"

namespace DCPcg
{
	uint32 Hash(int32 Seed, uint32 Salt, int32 Index)
	{
		uint32 H = static_cast<uint32>(Seed) * 0x9E3779B1u ^ Salt * 0x85EBCA77u ^ static_cast<uint32>(Index) * 0xC2B2AE3Du;
		H ^= H >> 16;
		H *= 0x7FEB352Du;
		H ^= H >> 15;
		H *= 0x846CA68Bu;
		H ^= H >> 16;
		return H;
	}

	float Unit(int32 Seed, uint32 Salt, int32 Index)
	{
		return static_cast<float>(Hash(Seed, Salt, Index) & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	namespace
	{
		uint32 SlotSalt(const FDCPcgRule& Rule)
		{
			return GetTypeHash(Rule.Slot.ToString());
		}

		FName PickSalted(const FDCPcgRule& Rule, int32 Seed, int32 Index, uint32 Extra)
		{
			float Total = 0.f;
			for (const FDCPcgChoice& C : Rule.Choices)
			{
				Total += FMath::Max(C.Weight, 0.f);
			}
			if (Total <= 0.f)
			{
				return NAME_None;
			}
			float Roll = Unit(Seed, SlotSalt(Rule) + Extra * 0x632BE5ABu, Index) * Total;
			for (const FDCPcgChoice& C : Rule.Choices)
			{
				Roll -= FMath::Max(C.Weight, 0.f);
				if (Roll < 0.f)
				{
					return C.Value;
				}
			}
			return Rule.Choices.Last().Value;
		}
	}

	FName Pick(const FDCPcgRule& Rule, int32 Seed, int32 Index)
	{
		return PickSalted(Rule, Seed, Index, 0);
	}

	float Range(const FDCPcgRule& Rule, int32 Seed, int32 Index)
	{
		return FMath::Lerp(Rule.Min, FMath::Max(Rule.Min, Rule.Max), Unit(Seed, SlotSalt(Rule) ^ 0x51ED27u, Index));
	}

	int32 RangeInt(const FDCPcgRule& Rule, int32 Seed, int32 Index)
	{
		const int32 Lo = FMath::RoundToInt32(Rule.Min);
		const int32 Hi = FMath::Max(Lo, FMath::RoundToInt32(Rule.Max));
		return Lo + static_cast<int32>(Hash(Seed, SlotSalt(Rule) ^ 0x51ED27u, Index) % static_cast<uint32>(Hi - Lo + 1));
	}

	TArray<FName> PickGrid(const FDCPcgRule& Rule, int32 Seed, int32 W, int32 H)
	{
		TArray<FName> Out;
		Out.SetNum(W * H);
		const bool bMirrorX = Rule.Mirror == TEXT("X");
		const bool bMirrorY = Rule.Mirror == TEXT("Y");
		auto Conflicts = [&](int32 X, int32 Y, FName V)
		{
			if (!Rule.NoSameNeighbour.Contains(V))
			{
				return false;
			}
			static const FIntPoint Around[4] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
			for (const FIntPoint& D : Around)
			{
				const int32 Nx = X + D.X, Ny = Y + D.Y;
				if (Nx >= 0 && Ny >= 0 && Nx < W && Ny < H && Out[Ny * W + Nx] == V)
				{
					return true;
				}
			}
			return false;
		};
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				// Symmetry: the mirrored half copies its twin (already filled, row-major order).
				const int32 Mx = bMirrorX && X >= (W + 1) / 2 ? W - 1 - X : X;
				const int32 My = bMirrorY && Y >= (H + 1) / 2 ? H - 1 - Y : Y;
				if (Mx != X || My != Y)
				{
					Out[Y * W + X] = Out[My * W + Mx];
					continue;
				}
				FName V = PickSalted(Rule, Seed, Y * W + X, 0);
				for (uint32 Try = 1; Try < 6 && Conflicts(X, Y, V); ++Try)
				{
					V = PickSalted(Rule, Seed, Y * W + X, Try);
				}
				if (Conflicts(X, Y, V))
				{
					const FDCPcgChoice* Free = Rule.Choices.FindByPredicate([&](const FDCPcgChoice& C) { return C.Weight > 0.f && !Conflicts(X, Y, C.Value); });
					V = Free ? Free->Value : V;
				}
				Out[Y * W + X] = V;
			}
		}
		return Out;
	}
}
