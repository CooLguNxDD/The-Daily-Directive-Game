#include "Voxel/DCVoxelSettings.h"

#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelOps.h"

const FDCVoxelSectorLayout& UDCVoxelSettings::SectorLayout(int32 Sector) const
{
	static const FDCVoxelSectorLayout Empty;
	return Sectors.Num() == 0 ? Empty : Sectors[FMath::Clamp(Sector, 0, Sectors.Num() - 1)];
}

TArray<FColor> UDCVoxelSettings::MeshColors() const
{
	TArray<FColor> Out;
	Out.Add(FColor::Transparent);
	for (const FDCVoxelMaterial& M : Palette)
	{
		FColor C = M.Color.ToFColor(true);
		C.A = static_cast<uint8>(FMath::Clamp(M.Glow, 0.f, 1.f) * 255.f);
		Out.Add(C);
	}
	return Out;
}

FDCVoxelCraftContext UDCVoxelSettings::CraftContext(float BlockSizeCm) const
{
	FDCVoxelCraftContext Ctx;
	Ctx.Palette = &Palette;
	Ctx.Recipes = &Recipes;
	Ctx.Tuning = &Tuning;
	Ctx.BayCm = FMath::RoundToInt32(BlockSizeCm);
	return Ctx;
}

FDCVoxelOpContext UDCVoxelSettings::OpContext(float BlockSizeCm) const
{
	FDCVoxelOpContext Ctx;
	Ctx.Tuning = &Tuning;
	Ctx.Carves = &Carves;
	Ctx.BayCm = FMath::RoundToInt32(BlockSizeCm);
	Ctx.RubbleOf.SetNumZeroed(Palette.Num() + 1);
	for (int32 i = 0; i < Palette.Num(); ++i)
	{
		const FName As = Palette[i].RubbleAs;
		const int32 Target = As.IsNone() ? INDEX_NONE : Palette.IndexOfByPredicate([As](const FDCVoxelMaterial& M) { return M.Id == As; });
		Ctx.RubbleOf[i + 1] = static_cast<uint8>(Target == INDEX_NONE ? i + 1 : Target + 1);
	}
	return Ctx;
}
