// Voxel city data (R1, R2): palette, recipes, sector layouts, carves and engine tuning, all from
// Config/DefaultGame.ini [/Script/Epic_AI_Mega_Jam2.DCVoxelSettings]. Code reads it; nothing here holds logic.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Voxel/DCVoxelTypes.h"
#include "Rules/DCPcg.h"
#include "DCVoxelSettings.generated.h"

struct FDCVoxelCraftContext;
struct FDCVoxelOpContext;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Director's Cut Voxel City"))
class EPIC_AI_MEGA_JAM2_API UDCVoxelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDCVoxelSettings& Get() { return *GetDefault<UDCVoxelSettings>(); }

	/** The city switch: Voxel builds the voxel city, Blocks keeps the first run's block towers. */
	UPROPERTY(EditAnywhere, Config, Category = "City") FName Mode = TEXT("Voxel");
	UPROPERTY(EditAnywhere, Config, Category = "City") FDCVoxelTuning Tuning;
	UPROPERTY(EditAnywhere, Config, Category = "City") TArray<FDCVoxelMaterial> Palette;
	UPROPERTY(EditAnywhere, Config, Category = "City") TArray<FDCVoxelRecipe> Recipes;
	/** One row per sector (the last row repeats for later sectors). */
	UPROPERTY(EditAnywhere, Config, Category = "City") TArray<FDCVoxelSectorLayout> Sectors;
	UPROPERTY(EditAnywhere, Config, Category = "City") TArray<FDCVoxelCarveDef> Carves;
	/** City builder (R16): one district row per sector (the last repeats); PCG rule sets they roll (R15). */
	UPROPERTY(EditAnywhere, Config, Category = "District") TArray<FDCDistrictRule> Districts;
	UPROPERTY(EditAnywhere, Config, Category = "District") TArray<FDCPcgRuleSet> RuleSets;
	/** City props (R2): the prop layers every district can turn on through its prop rule set. */
	UPROPERTY(EditAnywhere, Config, Category = "District") TArray<FDCPropLayer> PropLayers;
	/** Decay effects (R4): puff kinds and the emitters the city plays at a crater, a landing and burning rubble. */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") TArray<FDCFxKind> FxKinds;
	UPROPERTY(EditAnywhere, Config, Category = "Fx") TArray<FDCFxEmitter> FxEmitters;
	/** R25: props that light the street (empty = the legacy LampModel row only). */
	UPROPERTY(EditAnywhere, Config, Category = "Fx") TArray<FDCPropLightDef> PropLights;
	/** Vertex-colour material for voxel meshes (BaseColor = sRGB vertex colour, Emissive = alpha glow). */
	UPROPERTY(EditAnywhere, Config, Category = "City") FSoftObjectPath VoxelMaterial = FSoftObjectPath(TEXT("/Game/DC/Materials/M_DCVoxel.M_DCVoxel"));

	bool IsVoxelCity() const { return Mode == TEXT("Voxel") && Recipes.Num() > 0 && Palette.Num() > 0; }
	const FDCVoxelSectorLayout& SectorLayout(int32 Sector) const;
	const FDCDistrictRule* District(int32 Sector) const { return Districts.Num() == 0 ? nullptr : &Districts[FMath::Clamp(Sector, 0, Districts.Num() - 1)]; }
	const FDCPcgRuleSet* RuleSet(FName Id) const { return RuleSets.FindByPredicate([Id](const FDCPcgRuleSet& R) { return R.Id == Id; }); }
	/** Mesher colours: index = material id (0 = air). */
	TArray<FColor> MeshColors() const;
	FDCVoxelCraftContext CraftContext(float BlockSizeCm) const;
	FDCVoxelOpContext OpContext(float BlockSizeCm) const;
	/** Voxels per bay (one floor) in a tower grid. */
	int32 TowerBayVoxels(float BlockSizeCm) const { return FMath::Max(6, FMath::RoundToInt32(BlockSizeCm) / FMath::Max(Tuning.TowerVoxelCm, 1)); }
};
