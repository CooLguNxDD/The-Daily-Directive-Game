#include "DCOutfitBuild.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "DCTuning.h"
#include "Engine/SkeletalMesh.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	/** Meshes of model + swatch pair, built once (game thread). */
	TMap<FString, FDCVoxelMeshData>& MeshCache()
	{
		static TMap<FString, FDCVoxelMeshData> Cache;
		return Cache;
	}

	const FDCVoxelMeshData* PartMesh(FName Model, FName Main, FName Accent)
	{
		const FString Key = FString::Printf(TEXT("%s|%s|%s"), *Model.ToString(), *Main.ToString(), *Accent.ToString());
		if (const FDCVoxelMeshData* Found = MeshCache().Find(Key))
		{
			return Found;
		}
		const FDCVoxelModel* Parsed = DCVoxelModels::Parsed(Model);
		if (!Parsed)
		{
			return nullptr;
		}
		// The shipped mesh colours (index 0 = air, then the palette; alpha = glow) with the two swatch slots recoloured.
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		TArray<FName> Names;
		for (const FDCVoxelMaterial& M : V.Palette)
		{
			Names.Add(M.Id);
		}
		TArray<FColor> Colors = V.MeshColors();
		for (const TPair<FName, FName>& Swap : {TPair<FName, FName>(TEXT("SwatchMain"), Main), TPair<FName, FName>(TEXT("SwatchAccent"), Accent)})
		{
			const int32 Slot = Names.IndexOfByKey(Swap.Key);
			const int32 Pick = Names.IndexOfByKey(Swap.Value);
			if (Slot != INDEX_NONE && Pick != INDEX_NONE)
			{
				Colors[Slot + 1] = Colors[Pick + 1];
			}
		}
		FDCVoxelMeshData Mesh;
		Parsed->BuildMesh(Names, Colors, Mesh);
		return &MeshCache().Add(Key, MoveTemp(Mesh));
	}

	/** A part's transform relative to its bone, from data in actor space (X forward, Z up) at the reference pose. */
	FTransform RelativeToBone(const USkeletalMeshComponent* Mesh, const FDCOutfitPart& P)
	{
		const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
		const int32 Bone = Asset ? Asset->GetRefSkeleton().FindBoneIndex(P.Socket) : INDEX_NONE;
		if (Bone == INDEX_NONE)
		{
			return FTransform(P.Rotation, P.Offset, FVector(P.Scale));
		}
		const FTransform BoneCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), Bone);
		const FQuat MeshToActor = Mesh->GetRelativeRotation().Quaternion();
		const FQuat Rot = MeshToActor.Inverse() * P.Rotation.Quaternion();
		const FVector Loc = BoneCS.GetLocation() + MeshToActor.Inverse().RotateVector(P.Offset);
		return FTransform(Rot, Loc, FVector(P.Scale)).GetRelativeTransform(BoneCS);
	}
}

namespace DCOutfitBuild
{
	FDCOutfit FromCode(const FString& Code)
	{
		const UDCTuning& T = UDCTuning::Get();
		FDCOutfit Outfit;
		FString Error;
		return DCOutfit::Decode(Code, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), Outfit, Error) ? Outfit : DCOutfit::Default(T.OutfitSlots);
	}

	UProceduralMeshComponent* Apply(USkeletalMeshComponent* Mesh, const FDCOutfit& Outfit, TArray<TObjectPtr<UProceduralMeshComponent>>& Out)
	{
		for (UProceduralMeshComponent* Old : Out)
		{
			if (Old)
			{
				Old->DestroyComponent();
			}
		}
		Out.Reset();
		if (!Mesh || !Mesh->GetOwner())
		{
			return nullptr;
		}
		const UDCTuning& T = UDCTuning::Get();
		for (const FDCOutfitPart& P : T.OutfitParts)
		{
			if (!P.HideBone.IsNone())
			{
				Mesh->UnHideBoneByName(P.HideBone);
			}
		}
		const FName Main = T.OutfitSwatches.IsValidIndex(Outfit.Main) ? T.OutfitSwatches[Outfit.Main].Material : NAME_None;
		const FName Accent = T.OutfitSwatches.IsValidIndex(Outfit.Accent) ? T.OutfitSwatches[Outfit.Accent].Material : NAME_None;
		UMaterialInterface* Material = Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad());
		UProceduralMeshComponent* Camera = nullptr;
		for (int32 s = 0; s < T.OutfitSlots.Num() && s < Outfit.Parts.Num(); ++s)
		{
			const TArray<const FDCOutfitPart*> Parts = DCOutfit::PartsOf(T.OutfitSlots[s], T.OutfitParts);
			const FDCOutfitPart* P = Parts.IsValidIndex(Outfit.Parts[s]) ? Parts[Outfit.Parts[s]] : nullptr;
			const FDCVoxelMeshData* Data = P && !P->Model.IsNone() ? PartMesh(P->Model, Main, Accent) : nullptr;
			if (!Data)
			{
				continue;
			}
			UProceduralMeshComponent* Part = NewObject<UProceduralMeshComponent>(Mesh->GetOwner());
			Part->SetupAttachment(Mesh, P->Socket);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(true);
			Part->RegisterComponent();
			Part->CreateMeshSection(0, Data->Positions, Data->Indices, Data->Normals, Data->UVs, Data->Colors, {}, false);
			Part->SetMaterial(0, Material);
			Part->SetRelativeTransform(RelativeToBone(Mesh, *P));
			if (!P->HideBone.IsNone())
			{
				Mesh->HideBoneByName(P->HideBone, PBO_None);
			}
			Camera = T.OutfitSlots[s] == TEXT("Camera") ? Part : Camera;
			Out.Add(Part);
		}
		return Camera;
	}
}
