#include "Rules/DCPostStack.h"

#include "Algo/StableSort.h"

namespace DCPostStack
{
	FDCPostResult Compose(const TArray<FDCPostLayer>& Layers, const TMap<FName, float>& Weights, int32 Quality)
	{
		TArray<const FDCPostLayer*> Order;
		for (const FDCPostLayer& L : Layers)
		{
			Order.Add(&L);
		}
		// Stable: equal priorities keep their row order.
		Algo::StableSortBy(Order, [](const FDCPostLayer* L) { return L->Priority; });
		FDCPostResult R;
		float BloomCap = TNumericLimits<float>::Max();
		float ExposureCap = TNumericLimits<float>::Max();
		for (const FDCPostLayer* L : Order)
		{
			const float* Set = Weights.Find(L->Id);
			const float W = Set ? *Set : L->DefaultWeight;
			if (W <= 0.f || L->MinQuality > Quality)
			{
				continue;
			}
			R.Bloom += W * L->Bloom;
			R.Exposure += W * L->Exposure;
			R.Vignette += W * L->Vignette;
			R.Grain += W * L->Grain;
			R.Outline += W * L->Outline;
			R.BloomThreshold += W * L->BloomThreshold;
			R.Saturation *= FMath::Lerp(1.f, L->Saturation, FMath::Min(W, 1.f));
			R.Contrast *= FMath::Lerp(1.f, L->Contrast, FMath::Min(W, 1.f));
			R.Gain *= FMath::Lerp(FLinearColor::White, L->Gain, FMath::Min(W, 1.f));
			BloomCap = L->BloomCap > 0.f ? FMath::Min(BloomCap, L->BloomCap) : BloomCap;
			ExposureCap = L->ExposureCap != 0.f ? FMath::Min(ExposureCap, L->ExposureCap) : ExposureCap;
		}
		R.Bloom = FMath::Min(R.Bloom, BloomCap);
		R.Exposure = FMath::Min(R.Exposure, ExposureCap);
		R.Bloom = FMath::Max(R.Bloom, 0.f);
		R.Vignette = FMath::Clamp(R.Vignette, 0.f, 4.f);
		R.Grain = FMath::Clamp(R.Grain, 0.f, 2.f);
		R.Outline = FMath::Clamp(R.Outline, 0.f, 1.f);
		return R;
	}
}
