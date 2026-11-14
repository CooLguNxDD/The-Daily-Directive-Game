#include "Rules/DCNewsprint.h"

namespace
{
	float Luma(const FColor& C)
	{
		return 0.299f * C.R + 0.587f * C.G + 0.114f * C.B;
	}

	uint32 PixelHash(uint32 X, uint32 Y, uint32 Seed)
	{
		uint32 H = X * 0x8DA6B343u ^ Y * 0xD8163841u ^ Seed * 0xCB1AB31Fu;
		H ^= H >> 13;
		H *= 0x5BD1E995u;
		H ^= H >> 15;
		return H;
	}

	uint8 Clamp255(float V)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V), 0, 255));
	}
}

namespace DCNewsprint
{
	void Levels(const TArray<FColor>& Pixels, const FDCPrintTuning& T, float& OutGain, float& OutOffset)
	{
		OutGain = 1.f;
		OutOffset = 0.f;
		if (Pixels.Num() == 0)
		{
			return;
		}
		int32 Histogram[256] = {};
		for (const FColor& C : Pixels)
		{
			++Histogram[Clamp255(Luma(C))];
		}
		auto Percentile = [&Histogram, &Pixels](float P)
		{
			const int64 Target = static_cast<int64>(FMath::Clamp(P, 0.f, 1.f) * (Pixels.Num() - 1));
			int64 Sum = 0;
			for (int32 i = 0; i < 256; ++i)
			{
				Sum += Histogram[i];
				if (Sum > Target)
				{
					return static_cast<float>(i);
				}
			}
			return 255.f;
		};
		const float Lo = FMath::Min(Percentile(T.LowPercentile), FMath::Max(T.MaxBlackPoint, 0.f));
		const float Hi = FMath::Max(Percentile(T.HighPercentile), Lo + 1.f);
		OutGain = FMath::Min(255.f / (Hi - Lo), FMath::Max(T.MaxGain, 1.f));
		OutOffset = -Lo * OutGain;
	}

	float MidGamma(const TArray<FColor>& Pixels, const FDCPrintTuning& T, float Gain, float Offset)
	{
		if (Pixels.Num() == 0)
		{
			return 1.f;
		}
		int32 Histogram[256] = {};
		for (const FColor& C : Pixels)
		{
			++Histogram[Clamp255(Luma(C) * Gain + Offset)];
		}
		int64 Sum = 0;
		int32 Median = 255;
		for (int32 i = 0; i < 256; ++i)
		{
			Sum += Histogram[i];
			if (Sum * 2 > Pixels.Num())
			{
				Median = i;
				break;
			}
		}
		const float M = FMath::Clamp(Median / 255.f, 0.01f, 0.99f);
		const float Target = FMath::Clamp(T.TargetMid, 0.05f, 0.95f);
		return M >= Target ? 1.f : FMath::Clamp(FMath::Loge(Target) / FMath::Loge(M), FMath::Clamp(T.MinGamma, 0.1f, 1.f), 1.f);
	}

	float MeterGain(const TArray<FLinearColor>& Px, int32 W, int32 H, const FDCPrintTuning& T)
	{
		if (T.MeterKey <= 0.f || Px.Num() != W * H || W <= 0 || H <= 0)
		{
			return 1.f;
		}
		// Centre-weighted log average of linear luminance (the subject is framed near the centre), sparse samples.
		double Sum = 0.0, Weights = 0.0;
		const int32 Step = FMath::Max(1, FMath::Min(W, H) / 64);
		for (int32 Y = 0; Y < H; Y += Step)
		{
			for (int32 X = 0; X < W; X += Step)
			{
				const FLinearColor& C = Px[Y * W + X];
				const float L = 0.2126f * FMath::Pow(FMath::Clamp(C.R, 0.f, 1.f), 2.2f) + 0.7152f * FMath::Pow(FMath::Clamp(C.G, 0.f, 1.f), 2.2f) + 0.0722f * FMath::Pow(FMath::Clamp(C.B, 0.f, 1.f), 2.2f);
				const float Dx = (X + 0.5f) / W - 0.5f, Dy = (Y + 0.5f) / H - 0.5f;
				const float Wt = 1.f + (T.MeterCenterWeight - 1.f) * FMath::Clamp(1.f - 4.f * (Dx * Dx + Dy * Dy), 0.f, 1.f);
				Sum += Wt * FMath::Loge(FMath::Max(L, 1e-4f));
				Weights += Wt;
			}
		}
		const float LogAvg = FMath::Exp(static_cast<float>(Sum / FMath::Max(Weights, 1e-6)));
		const float Gain = T.MeterKey / FMath::Max(LogAvg, 1e-4f);
		return FMath::Clamp(Gain, FMath::Pow(2.f, -T.MeterMinStops), FMath::Pow(2.f, T.MeterMaxStops));
	}

	void Develop(const TArray<FLinearColor>& In, int32 W, int32 H, const FDCPrintTuning& T, TArray<FColor>& Out)
	{
		const float Gain = MeterGain(In, W, H, T);
		const float White = FMath::Max(T.MeterWhite, 1.f);
		Out.SetNumUninitialized(In.Num());
		for (int32 i = 0; i < In.Num(); ++i)
		{
			const FLinearColor& C = In[i];
			auto Ch = [Gain, White](float V)
			{
				// Decode, gain, shoulder (Reinhard with a white point: 1 at White), encode.
				const float Lin = FMath::Pow(FMath::Clamp(V, 0.f, 1.f), 2.2f) * Gain;
				const float Mapped = Lin * (1.f + Lin / (White * White)) / (1.f + Lin);
				return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(255.f * FMath::Pow(FMath::Clamp(Mapped, 0.f, 1.f), 1.f / 2.2f)), 0, 255));
			};
			Out[i] = FColor(Ch(C.R), Ch(C.G), Ch(C.B), 255);
		}
	}

	void Print(TArray<FColor>& Pixels, int32 W, int32 H, const FDCPrintTuning& T, uint32 Seed)
	{
		if (Pixels.Num() != W * H || W <= 0 || H <= 0)
		{
			return;
		}
		float Gain, Offset;
		Levels(Pixels, T, Gain, Offset);
		const float Gamma = MidGamma(Pixels, T, Gain, Offset);
		// Levels then the mid-tone lift, applied to every pixel once (colour scaled by the luma change).
		for (FColor& C : Pixels)
		{
			const float L = FMath::Clamp(Luma(C) * Gain + Offset, 0.f, 255.f);
			const float Lifted = 255.f * FMath::Pow(L / 255.f, Gamma);
			const float K = Gain * (L > 0.5f ? Lifted / L : 1.f);
			const float B = Offset * (L > 0.5f ? Lifted / L : 1.f);
			C = FColor(Clamp255(C.R * K + B), Clamp255(C.G * K + B), Clamp255(C.B * K + B), C.A);
		}
		Gain = 1.f;
		Offset = 0.f;
		if (T.ChromaSplitPx > 0)
		{
			// Feed chromatic split: red from the left, blue from the right, green in place (clamped at the edges).
			const TArray<FColor> Src = Pixels;
			const int32 Split = T.ChromaSplitPx;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					FColor& C = Pixels[Y * W + X];
					C.R = Src[Y * W + FMath::Max(X - Split, 0)].R;
					C.B = Src[Y * W + FMath::Min(X + Split, W - 1)].B;
				}
			}
		}
		const int32 Cell = FMath::Max(T.DotCell, 0);
		// Mean luminance per halftone cell (after levels), for the dot size.
		const int32 Cw = Cell > 0 ? FMath::DivideAndRoundUp(W, Cell) : 0;
		const int32 Ch = Cell > 0 ? FMath::DivideAndRoundUp(H, Cell) : 0;
		TArray<float> CellLuma;
		if (Cell > 0)
		{
			CellLuma.SetNumZeroed(Cw * Ch);
			TArray<int32> Count;
			Count.SetNumZeroed(Cw * Ch);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const int32 I = (Y / Cell) * Cw + X / Cell;
					CellLuma[I] += FMath::Clamp(Luma(Pixels[Y * W + X]) * Gain + Offset, 0.f, 255.f);
					++Count[I];
				}
			}
			for (int32 I = 0; I < CellLuma.Num(); ++I)
			{
				CellLuma[I] /= FMath::Max(Count[I], 1);
			}
		}
		const FLinearColor InkC = FLinearColor(T.Ink.R, T.Ink.G, T.Ink.B);
		const FLinearColor PaperC = FLinearColor(T.Paper.R, T.Paper.G, T.Paper.B);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				FColor& Px = Pixels[Y * W + X];
				const bool bBorder = X < T.BorderPx || Y < T.BorderPx || X >= W - T.BorderPx || Y >= H - T.BorderPx;
				const bool bRule = !bBorder && (X < T.BorderPx + T.RulePx || Y < T.BorderPx + T.RulePx || X >= W - T.BorderPx - T.RulePx || Y >= H - T.BorderPx - T.RulePx);
				if (bBorder || bRule)
				{
					const FLinearColor& Edge = bBorder ? PaperC : InkC;
					Px = FColor(Clamp255(Edge.R), Clamp255(Edge.G), Clamp255(Edge.B), 255);
					continue;
				}
				// Levels, then the photo's colour pulled toward the paper tone (sepia-like duotone).
				const FLinearColor Src(Px.R * Gain + Offset, Px.G * Gain + Offset, Px.B * Gain + Offset);
				const float L = FMath::Clamp(0.299f * Src.R + 0.587f * Src.G + 0.114f * Src.B, 0.f, 255.f) / 255.f;
				const FLinearColor Tone = FMath::Lerp(InkC, PaperC, L);
				FLinearColor Out = FMath::Lerp(Tone, Src, FMath::Clamp(T.ColorKeep, 0.f, 1.f));
				if (Cell > 0)
				{
					// Ink dot centred in the cell; its radius grows with darkness (area ~ coverage).
					const float Dark = 1.f - CellLuma[(Y / Cell) * Cw + X / Cell] / 255.f;
					const float R = FMath::Sqrt(Dark / PI) * Cell; // dot area = darkness x cell area
					const float Dx = (X % Cell) + 0.5f - Cell * 0.5f;
					const float Dy = (Y % Cell) + 0.5f - Cell * 0.5f;
					const float Inside = FMath::Clamp(R - FMath::Sqrt(Dx * Dx + Dy * Dy) + 0.5f, 0.f, 1.f);
					const FLinearColor Dot = FMath::Lerp(PaperC, InkC, Inside);
					Out = FMath::Lerp(Out, Dot, FMath::Clamp(T.HalftoneMix, 0.f, 1.f));
				}
				if (T.ScanlineEvery > 0 && Y % T.ScanlineEvery == 0)
				{
					Out *= 1.f - FMath::Clamp(T.ScanlineDark, 0.f, 1.f); // feed scanlines
				}
				const float N = (static_cast<float>(PixelHash(X, Y, Seed) & 0xFFFF) / 65535.f - 0.5f) * 2.f * T.Grain;
				Px = FColor(Clamp255(Out.R + N), Clamp255(Out.G + N), Clamp255(Out.B + N), 255);
			}
		}
	}
}
