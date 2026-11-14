// Loads the handful of soft-referenced assets once (at BeginPlay time, never in Tick) and makes flat palette
// materials. Every getter falls back to engine content so a missing asset never breaks a run.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;

namespace DCAssets
{
	EPIC_AI_MEGA_JAM2_API UStaticMesh* Cube();
	EPIC_AI_MEGA_JAM2_API UStaticMesh* Sphere();
	EPIC_AI_MEGA_JAM2_API UStaticMesh* Cylinder();
	EPIC_AI_MEGA_JAM2_API UStaticMesh* Cone();
	/** A thin ground ring of cube segments (the danger zone and the aim marker), sized from the Look tuning. */
	EPIC_AI_MEGA_JAM2_API void BuildRing(UInstancedStaticMeshComponent* Ism, float RadiusCm);
	EPIC_AI_MEGA_JAM2_API UMaterialInterface* FlatMaterial();

	/** Flat colour, optional glow (emissive = Color * Glow; needs the project's M_DCFlat). */
	EPIC_AI_MEGA_JAM2_API UMaterialInstanceDynamic* MakeFlat(UObject* Outer, const FLinearColor& Color, float Glow = 0.f);
	/** Unlit: only Color * Glow shows (no sun on it), so a danger red stays the palette red. Same parameters as MakeFlat. */
	EPIC_AI_MEGA_JAM2_API UMaterialInstanceDynamic* MakeFlatUnlit(UObject* Outer, const FLinearColor& Color, float Glow = 1.f);
	EPIC_AI_MEGA_JAM2_API void SetFlat(UMaterialInstanceDynamic* Mid, const FLinearColor& Color, float Glow);
}
