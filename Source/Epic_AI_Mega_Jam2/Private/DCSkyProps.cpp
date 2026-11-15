// R12 sky detail and distant skyline: +SkyProps rows place forge models (airships, spires, masts) in a ring past the
// sector edge, seeded per sector so every machine sees the same skyline. Drifting rows orbit the city slowly. Cosmetic
// only: no collision, no shadows, a handful of meshes (the voxel mesher already merged their faces).
#include "DCEnvironment.h"
#include "DCTuning.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

void ADCEnvironment::BuildSkyProps(int32 RunSeed, int32 SectorIndex, float HalfCm)
{
	for (UProceduralMeshComponent* Mesh : SkyMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	SkyMeshes.Reset();
	SkyState.Reset();
	UMaterialInterface* VoxelMaterial = Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad());
	FRandomStream Rng(HashCombine(GetTypeHash(RunSeed), GetTypeHash(SectorIndex * 6151 + 29)));
	for (const FDCSkyPropDef& Row : UDCTuning::Get().SkyProps)
	{
		const FDCVoxelMeshData* Model = DCVoxelModels::Mesh(Row.Model);
		if (!Model)
		{
			continue; // a row naming a missing model is skipped (data is validated by Content.SkyProps)
		}
		for (int32 i = 0; i < FMath::Clamp(Row.Count, 0, 32); ++i)
		{
			FSkyProp P;
			P.Angle = 2.f * PI * (i + Rng.FRand() * 0.7f) / FMath::Max(Row.Count, 1) + Rng.FRand() * PI;
			// The sector is a square: the ring starts outside its corners along each direction.
			const FVector2D Dir(FMath::Cos(P.Angle), FMath::Sin(P.Angle));
			const float Square = HalfCm / FMath::Max(FMath::Max(FMath::Abs(Dir.X), FMath::Abs(Dir.Y)), 0.01f);
			P.Radius = Square + Rng.FRandRange(Row.RingCm.X, Row.RingCm.Y);
			P.Height = Rng.FRandRange(Row.HeightCm.X, Row.HeightCm.Y);
			P.OrbitDegPerSec = Row.OrbitDegPerSec * (Rng.FRand() < 0.5f ? 1.f : -1.f);
			P.BobCm = Row.BobCm;
			P.Phase = Rng.FRand() * 2.f * PI;
			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
			Mesh->SetupAttachment(GetRootComponent());
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->RegisterComponent();
			Mesh->CreateMeshSection(0, Model->Positions, Model->Indices, Model->Normals, Model->UVs, Model->Colors, {}, false);
			Mesh->SetMaterial(0, VoxelMaterial);
			Mesh->SetWorldScale3D(FVector(Rng.FRandRange(Row.Scale.X, Row.Scale.Y)));
			SkyMeshes.Add(Mesh);
			SkyState.Add(P);
		}
	}
	UpdateSkyProps(0.f);
	UE_LOG(LogTemp, Display, TEXT("DCSKY sector=%d meshes=%d first=%s"), SectorIndex, SkyMeshes.Num(), SkyMeshes.Num() > 0 ? *SkyMeshes[0]->GetComponentLocation().ToCompactString() : TEXT("none"));
}

void ADCEnvironment::UpdateSkyProps(float DeltaSeconds)
{
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (int32 i = 0; i < SkyMeshes.Num() && i < SkyState.Num(); ++i)
	{
		FSkyProp& P = SkyState[i];
		P.Angle += FMath::DegreesToRadians(P.OrbitDegPerSec) * DeltaSeconds;
		const FVector At(FMath::Cos(P.Angle) * P.Radius, FMath::Sin(P.Angle) * P.Radius, P.Height + P.BobCm * FMath::Sin(Time * 0.3f + P.Phase));
		// Standing props face the city; drifting ones fly along their orbit (the model faces +x).
		const float Yaw = FMath::RadiansToDegrees(P.Angle) + (P.OrbitDegPerSec == 0.f ? 180.f : (P.OrbitDegPerSec > 0.f ? 90.f : -90.f));
		SkyMeshes[i]->SetWorldLocationAndRotation(At, FRotator(0.f, Yaw, 0.f));
	}
}
