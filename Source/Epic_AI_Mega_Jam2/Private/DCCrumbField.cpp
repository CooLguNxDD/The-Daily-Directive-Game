#include "DCCrumbField.h"
#include "DCFrameScopes.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "DCAssets.h"
#include "DCTuning.h"
#include "Misc/ScopeExit.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	constexpr float CrumbGravityCms = 2400.f;
	/** The engine cube is 100 cm on an edge. */
	constexpr float CubeCm = 100.f;
}

UDCCrumbField::UDCCrumbField()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

int32 UDCCrumbField::BucketFor(const FLinearColor& InColor, float Glow)
{
	// Unlit chips keep their hue, never a paint-bright albedo (same cap as the debris pool, critic peak glare).
	const float Cap = UDCTuning::Get().Look.DebrisAlbedoCap;
	const float Max = InColor.GetMax();
	const FLinearColor Color = (Glow <= 0.f && Cap > 0.f && Max > Cap) ? InColor * (Cap / Max) : InColor;
	// Quantize so a collapse's crumbs share a few buckets (draw calls), not one per shade.
	const FColor Q = Color.ToFColor(true);
	const uint32 Key = ((Q.R >> 3) << 18) | ((Q.G >> 3) << 13) | ((Q.B >> 3) << 8) | static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(Glow * 4.f), 0, 255));
	if (const int32* Found = BucketByKey.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	Ism->SetStaticMesh(DCAssets::Cube());
	const FLinearColor BucketColor = FLinearColor::FromSRGBColor(FColor((Q.R >> 3) << 3, (Q.G >> 3) << 3, (Q.B >> 3) << 3));
	const float BucketGlow = FMath::RoundToInt(Glow * 4.f) / 4.f;
	UMaterialInstanceDynamic* Dissolve = DCAssets::MakeFlatDissolve(GetOwner(), BucketColor, BucketGlow); // R15
	Ism->SetMaterial(0, Dissolve ? Dissolve : DCAssets::MakeFlat(GetOwner(), BucketColor, BucketGlow));
	Ism->SetNumCustomDataFloats(Dissolve ? 1 : 0);
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(false);
	Ism->SetMobility(EComponentMobility::Movable);
	Ism->SetupAttachment(this);
	Ism->RegisterComponent();
	Isms.Add(Ism);
	FBucket& B = Buckets.AddDefaulted_GetRef();
	B.Ism = Ism;
	B.bDissolve = Dissolve != nullptr;
	BucketByKey.Add(Key, Buckets.Num() - 1);
	return Buckets.Num() - 1;
}

void UDCCrumbField::Spawn(const FDCCrumbSpawn& S)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (Tn.CrumbMax <= 0)
	{
		return;
	}
	if (SpawnedThisFrame >= Tn.CrumbSpawnPerFrame)
	{
		if (Queued.Num() < Tn.CrumbMax)
		{
			Queued.Add(S);
		}
		return;
	}
	++SpawnedThisFrame;
	SpawnNow(S);
}

void UDCCrumbField::SpawnNow(const FDCCrumbSpawn& S)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	int32 Index = INDEX_NONE;
	if (FreeCrumbs.Num() > 0)
	{
		Index = FreeCrumbs.Pop(EAllowShrinking::No);
	}
	else if (Crumbs.Num() < Tn.CrumbMax)
	{
		Index = Crumbs.AddDefaulted();
	}
	else
	{
		// Full: recycle round the ring (oldest-ish first, O(1)) so a fresh break always shows.
		RecycleCursor = (RecycleCursor + 1) % Crumbs.Num();
		Kill(RecycleCursor);
		Index = FreeCrumbs.Num() > 0 ? FreeCrumbs.Pop(EAllowShrinking::No) : RecycleCursor;
	}
	const int32 Bucket = BucketFor(S.Color, S.Glow);
	FBucket& B = Buckets[Bucket];
	FCrumb& C = Crumbs[Index];
	C = FCrumb();
	C.Bucket = Bucket;
	C.Pos = S.At;
	C.Vel = S.Velocity;
	C.SpinAxis = Rng.VRand();
	C.SpinDegS = Rng.FRandRange(180.f, 720.f);
	C.Size = S.SizeCm;
	C.Born = Clock;
	C.Life = Tn.CrumbLifeSeconds * Rng.FRandRange(0.6f, 1.2f);
	C.Gone = FMath::Max(Tn.CrumbShrinkSeconds, 0.01f) * (1.f + Rng.FRandRange(-1.f, 1.f) * FMath::Clamp(Tn.DissolveDurationJitter, 0.f, 0.9f));
	C.bAlive = true;
	const FTransform Xf(FQuat::Identity, C.Pos, FVector(C.Size / CubeCm));
	if (B.FreeInstances.Num() > 0)
	{
		C.Instance = B.FreeInstances.Pop(EAllowShrinking::No);
		B.Ism->UpdateInstanceTransform(C.Instance, Xf, true, false, true);
	}
	else
	{
		C.Instance = B.Ism->AddInstance(Xf, true);
	}
	if (B.bDissolve)
	{
		B.Ism->SetCustomDataValue(C.Instance, 0, 0.f, false);
	}
	B.bDirty = true;
	++Live;
}

void UDCCrumbField::Kill(int32 Index)
{
	FCrumb& C = Crumbs[Index];
	if (!C.bAlive)
	{
		return;
	}
	FBucket& B = Buckets[C.Bucket];
	// Parked instances shrink to nothing (instance indices stay stable; removal would reorder them).
	B.Ism->UpdateInstanceTransform(C.Instance, FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), true, false, true);
	B.FreeInstances.Add(C.Instance);
	B.bDirty = true;
	C.bAlive = false;
	FreeCrumbs.Add(Index);
	--Live;
}

void UDCCrumbField::ClearAll()
{
	Queued.Reset();
	for (int32 i = 0; i < Crumbs.Num(); ++i)
	{
		Kill(i);
	}
	for (FBucket& B : Buckets)
	{
		if (B.bDirty && B.Ism)
		{
			B.Ism->MarkRenderStateDirty();
			B.bDirty = false;
		}
	}
}

void UDCCrumbField::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	DC_FRAME_SCOPE("Crumbs");
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double T0 = FPlatformTime::Seconds();
	Clock += DeltaTime;
	SpawnedThisFrame = 0;
	const int32 Drain = FMath::Min(Queued.Num(), UDCVoxelSettings::Get().Tuning.CrumbSpawnPerFrame);
	for (int32 i = 0; i < Drain; ++i)
	{
		SpawnNow(Queued[i]);
	}
	Queued.RemoveAt(0, Drain, EAllowShrinking::No);
	SpawnedThisFrame = Drain;
	ON_SCOPE_EXIT { LastTickMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0); };
	if (Live == 0)
	{
		return;
	}
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const float Dt = FMath::Min(DeltaTime, 0.05f);
	for (int32 i = 0; i < Crumbs.Num(); ++i)
	{
		FCrumb& C = Crumbs[i];
		if (!C.bAlive)
		{
			continue;
		}
		const float Age = Clock - C.Born;
		if (Age >= C.Life + C.Gone)
		{
			Kill(i);
			continue;
		}
		if (!C.bResting)
		{
			C.Vel.Z -= CrumbGravityCms * Dt;
			C.Pos += C.Vel * Dt;
			C.AngleDeg += C.SpinDegS * Dt;
			const float Ground = GroundZ ? GroundZ(C.Pos) : 0.f;
			const float Floor = Ground + C.Size * 0.5f;
			if (C.Pos.Z < Floor)
			{
				C.Pos.Z = Floor;
				++C.Bounces;
				const bool bStop = C.Bounces > 2 || FMath::Abs(C.Vel.Z) < 120.f;
				C.Vel = bStop ? FVector::ZeroVector : FVector(C.Vel.X * 0.5f, C.Vel.Y * 0.5f, -C.Vel.Z * Tn.CrumbBounce);
				C.SpinDegS *= 0.4f;
				C.bResting = bStop;
				// A resting crumb lies flat-ish on a quarter turn (snapped so a pile reads as voxels, not dice).
				C.AngleDeg = bStop ? FMath::RoundToFloat(C.AngleDeg / 90.f) * 90.f : C.AngleDeg;
			}
		}
		else if (Age < C.Life)
		{
			continue; // lying still: nothing to update
		}
		// R15: past its life a crumb dissolves (noise with a warm edge) at full size; without the dissolve material it shrinks.
		const float Gone = Age > C.Life ? FMath::Clamp((Age - C.Life) / C.Gone, 0.f, 1.f) : 0.f;
		const FQuat Rot = C.bResting ? FQuat(FVector::UpVector, FMath::DegreesToRadians(C.AngleDeg)) : FQuat(C.SpinAxis, FMath::DegreesToRadians(C.AngleDeg));
		FBucket& B = Buckets[C.Bucket];
		B.Ism->UpdateInstanceTransform(C.Instance, FTransform(Rot, C.Pos, FVector(C.Size * (B.bDissolve ? 1.f : 1.f - Gone) / CubeCm)), true, false, true);
		if (B.bDissolve)
		{
			B.Ism->SetCustomDataValue(C.Instance, 0, Gone, false);
		}
		B.bDirty = true;
	}
	for (FBucket& B : Buckets)
	{
		if (B.bDirty && B.Ism)
		{
			B.Ism->MarkRenderStateDirty();
			B.bDirty = false;
		}
	}
}
