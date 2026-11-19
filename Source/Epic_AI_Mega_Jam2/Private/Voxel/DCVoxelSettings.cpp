#include "Voxel/DCVoxelSettings.h"

#include "Voxel/DCVoxelCraft.h"
#include "Voxel/DCVoxelOps.h"
#include "Rules/DCPcg.h"

const FDCVoxelSectorLayout& UDCVoxelSettings::SectorLayout(int32 Sector) const
{
	static const FDCVoxelSectorLayout Empty;
	return Sectors.Num() == 0 ? Empty : Sectors[FMath::Clamp(Sector, 0, Sectors.Num() - 1)];
}

TArray<const FDCDistrictRule*> UDCVoxelSettings::DistrictsFor(int32 Sector) const
{
	TArray<const FDCDistrictRule*> Out;
	for (const FDCDistrictRule& D : Districts)
	{
		if (D.Sector == Sector)
		{
			Out.Add(&D);
		}
	}
	if (Out.Num() == 0 && Districts.Num() > 0)
	{
		Out.Add(&Districts[FMath::Clamp(Sector, 0, Districts.Num() - 1)]);
	}
	return Out;
}

const FDCDistrictRule* UDCVoxelSettings::District(int32 Sector, int32 SectorSeed) const
{
	const TArray<const FDCDistrictRule*> Rows = DistrictsFor(Sector);
	if (Rows.Num() <= 1)
	{
		return Rows.Num() == 1 ? Rows[0] : nullptr;
	}
	FDCPcgRule Roll;
	Roll.Slot = TEXT("District");
	for (const FDCDistrictRule* D : Rows)
	{
		Roll.Choices.Add({D->Id, D->Weight});
	}
	const FName Id = DCPcg::Pick(Roll, SectorSeed ^ 0xD157C7, 0);
	const FDCDistrictRule* const* Found = Rows.FindByPredicate([Id](const FDCDistrictRule* D) { return D->Id == Id; });
	return Found ? *Found : Rows[0];
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
