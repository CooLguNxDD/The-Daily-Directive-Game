#include "DCAssets.h"

#include "DCTuning.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
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
