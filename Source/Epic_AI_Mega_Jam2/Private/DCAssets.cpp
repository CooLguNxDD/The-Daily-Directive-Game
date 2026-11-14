#include "DCAssets.h"

#include "DCTuning.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
	template <typename T>
	T* LoadCached(const FSoftObjectPath& Path, TObjectPtr<T>& Cache)
	{
		if (!Cache)
		{
			Cache = Cast<T>(Path.TryLoad());
			if (Cache)
			{
				Cache->AddToRoot();
			}
		}
		return Cache;
	}

	TObjectPtr<UStaticMesh> GCube, GSphere, GCylinder, GCone;
	TObjectPtr<UMaterialInterface> GFlat, GFallback, GFlatUnlit;

	const FName ColorParam(TEXT("Color"));
	const FName GlowParam(TEXT("Glow"));
}

namespace DCAssets
{
	UStaticMesh* Cube() { return LoadCached(UDCTuning::Get().Assets.Cube, GCube); }
	UStaticMesh* Sphere() { return LoadCached(UDCTuning::Get().Assets.Sphere, GSphere); }
	UStaticMesh* Cylinder() { return LoadCached(UDCTuning::Get().Assets.Cylinder, GCylinder); }
	UStaticMesh* Cone() { return LoadCached(UDCTuning::Get().Assets.Cone, GCone); }

	void BuildRing(UInstancedStaticMeshComponent* Ism, float RadiusCm)
	{
		if (!Ism)
		{
			return;
		}
		const FDCLookTuning& Look = UDCTuning::Get().Look;
		Ism->SetStaticMesh(Cube());
		Ism->ClearInstances();
		const int32 Segments = FMath::Max(Look.DangerRingSegments, 8);
		const float SegLen = 2.f * PI * RadiusCm / Segments * 1.04f;
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A = 2.f * PI * i / Segments;
			const FVector At(FMath::Cos(A) * RadiusCm, FMath::Sin(A) * RadiusCm, 0.f);
			Ism->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(A) + 90.f, 0.f), At, FVector(SegLen / 100.f, Look.DangerRingWidthCm / 100.f, 0.04f)));
		}
	}

	UMaterialInterface* FlatMaterial()
	{
		if (UMaterialInterface* Flat = LoadCached(UDCTuning::Get().Assets.FlatMaterial, GFlat))
		{
			return Flat;
		}
		return LoadCached(UDCTuning::Get().Assets.FallbackMaterial, GFallback);
	}

	void SetFlat(UMaterialInstanceDynamic* Mid, const FLinearColor& Color, float Glow)
	{
		if (!Mid)
		{
			return;
		}
		Mid->SetVectorParameterValue(ColorParam, Color);
		Mid->SetScalarParameterValue(GlowParam, Glow);
	}

	UMaterialInstanceDynamic* MakeFlat(UObject* Outer, const FLinearColor& Color, float Glow)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(FlatMaterial(), Outer);
		SetFlat(Mid, Color, Glow);
		return Mid;
	}

	UMaterialInstanceDynamic* MakeFlatUnlit(UObject* Outer, const FLinearColor& Color, float Glow)
	{
		UMaterialInterface* Unlit = LoadCached(UDCTuning::Get().Assets.FlatUnlitMaterial, GFlatUnlit);
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Unlit ? Unlit : FlatMaterial(), Outer);
		SetFlat(Mid, Color, Glow);
		return Mid;
	}
}
