#include "DCVoxelFx.h"
#include "DCPresentationClock.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "DCAssets.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Voxel/DCVoxelSettings.h"

ADCVoxelFx::ADCVoxelFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Rng.Initialize(0xF00D);
}

void ADCVoxelFx::BeginPlay()
{
	Super::BeginPlay();
	// One instanced cube per kind, prewarmed to its pool size (hidden at zero scale): no allocation per puff.
	for (const FDCFxKind& K : UDCVoxelSettings::Get().FxKinds)
	{
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this);
		Ism->SetupAttachment(RootComponent);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(K.Glow <= 0.f);
		Ism->RegisterComponent();
		Ism->SetStaticMesh(DCAssets::Cube());
		Ism->SetMaterial(0, DCAssets::MakeFlat(this, K.Color, K.Glow));
		TArray<FTransform> Hidden;
		Hidden.Init(FTransform(FQuat::Identity, FVector(0, 0, -100000.f), FVector::ZeroVector), FMath::Max(K.Pool, 1));
		Ism->AddInstances(Hidden, false);
		Meshes.Add(Ism);
		Puffs.AddDefaulted();
		Shown.Add(0);
	}
}

int32 ADCVoxelFx::KindIndex(FName Kind) const
{
	return UDCVoxelSettings::Get().FxKinds.IndexOfByPredicate([Kind](const FDCFxKind& K) { return K.Id == Kind; });
}

void ADCVoxelFx::Spawn(int32 Kind, const FVector& Where, float Radius, int32 Count)
{
	const FDCFxKind& K = UDCVoxelSettings::Get().FxKinds[Kind];
	TArray<FPuff>& List = Puffs[Kind];
	const float Now = FxTime;
	for (int32 i = 0; i < Count; ++i)
	{
		if (List.Num() >= K.Pool)
		{
			List.RemoveAt(0, EAllowShrinking::No); // the oldest puff makes room (bounded pool)
		}
		FPuff& P = List.AddDefaulted_GetRef();
		P.Kind = Kind;
		const FVector Dir = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		P.Pos = Where + Dir * Rng.FRandRange(0.f, Radius) + FVector(0, 0, Rng.FRandRange(0.f, 80.f));
		P.Vel = Dir * K.Speed * Rng.FRandRange(0.4f, 1.f) + FVector(0, 0, K.Rise * Rng.FRandRange(0.6f, 1.2f));
		P.Spin = FRotator(Rng.FRandRange(0, 90), Rng.FRandRange(0, 90), 0);
		P.SpinRate = FRotator(Rng.FRandRange(-K.SpinDeg, K.SpinDeg), Rng.FRandRange(-K.SpinDeg, K.SpinDeg), 0);
		P.Born = Now;
		P.Life = Rng.FRandRange(K.LifeMin, FMath::Max(K.LifeMin, K.LifeMax));
	}
}

void ADCVoxelFx::Emit(FName Emitter, const FVector& Where, float Radius)
{
	const FDCFxEmitter* E = UDCVoxelSettings::Get().FxEmitters.FindByPredicate([Emitter](const FDCFxEmitter& R) { return R.Id == Emitter; });
	if (!E)
	{
		return;
	}
	for (const FDCFxBurst& B : E->Bursts)
	{
		const int32 Kind = KindIndex(B.Kind);
		if (Puffs.IsValidIndex(Kind))
		{
			Spawn(Kind, Where, Radius, B.Count);
		}
	}
}

void ADCVoxelFx::SpawnKind(FName Kind, const FVector& Where, float Radius, int32 Count)
{
	const int32 K = KindIndex(Kind);
	if (Puffs.IsValidIndex(K) && Count > 0)
	{
		Spawn(K, Where, Radius, Count);
	}
}

void ADCVoxelFx::Burn(FName Emitter, const FVector& Where, float Radius, float Seconds)
{
	const float Now = FxTime;
	Burns.Add({Emitter, Where, Radius, Now + Seconds, Now});
}

void ADCVoxelFx::ClearAll()
{
	Burns.Reset();
	for (TArray<FPuff>& List : Puffs)
	{
		List.Reset();
	}
}

void ADCVoxelFx::Tick(float WorldDeltaSeconds)
{
	Super::Tick(WorldDeltaSeconds);
	// Puffs live on this machine's presentation clock (a dying player's Last Frame slows them, R21).
	const float DeltaSeconds = WorldDeltaSeconds * UDCPresentationClock::Dilation(this);
	FxTime += DeltaSeconds;
	const float Now = FxTime;
	for (int32 i = Burns.Num() - 1; i >= 0; --i)
	{
		FBurn& B = Burns[i];
		if (Now >= B.Until)
		{
			Burns.RemoveAtSwap(i);
			continue;
		}
		if (Now >= B.Next)
		{
			const FDCFxEmitter* E = UDCVoxelSettings::Get().FxEmitters.FindByPredicate([&B](const FDCFxEmitter& R) { return R.Id == B.Emitter; });
			B.Next = Now + (E ? FMath::Max(E->Interval, 0.05f) : 1.f);
			Emit(B.Emitter, B.Where, B.Radius);
		}
	}
	const TArray<FDCFxKind>& Kinds = UDCVoxelSettings::Get().FxKinds;
	TArray<FTransform> Xf;
	// Puffs shrink away near the local camera: a smoke cube never fills the lens (CP4 frames: the view went brown).
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const bool bHaveEye = PC && PC->PlayerCameraManager;
	const FVector Eye = bHaveEye ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	for (int32 k = 0; k < Puffs.Num() && k < Meshes.Num(); ++k)
	{
		const FDCFxKind& K = Kinds[k];
		TArray<FPuff>& List = Puffs[k];
		List.RemoveAll([Now](const FPuff& P) { return Now - P.Born >= P.Life; });
		Xf.Reset();
		const float DragK = FMath::Clamp(1.f - K.Drag * DeltaSeconds, 0.f, 1.f);
		for (FPuff& P : List)
		{
			P.Vel = P.Vel * DragK + FVector(0, 0, K.Gravity * DeltaSeconds);
			P.Pos += (P.Vel + Wind) * DeltaSeconds;
			P.Spin += P.SpinRate * DeltaSeconds;
			const float U = FMath::Clamp((Now - P.Born) / P.Life, 0.f, 1.f);
			// Pop in, grow toward the end size, shrink away in the last fifth: chunky, never a fade.
			const float Pop = FMath::Min(U * 8.f, 1.f) * FMath::Min((1.f - U) * 5.f, 1.f);
			const float Near = bHaveEye ? FMath::Clamp((FVector::Dist(P.Pos, Eye) - Tn.FxNearHideCm) / FMath::Max(Tn.FxNearFadeCm, 1.f), 0.f, 1.f) : 1.f;
			const float Size = FMath::Lerp(K.SizeStart, K.SizeEnd, U) * Pop * Near;
			Xf.Add(FTransform(K.Stretch > 1.f ? FRotator::ZeroRotator : P.Spin, P.Pos, FVector(Size / 100.f, Size / 100.f, Size * K.Stretch / 100.f)));
		}
		const int32 Live = Xf.Num();
		const int32 Touch = FMath::Min(FMath::Max(Live, Shown[k]), Meshes[k]->GetInstanceCount());
		if (Touch == 0)
		{
			continue; // nothing live now or last frame: the pool stays hidden untouched
		}
		while (Xf.Num() < Touch)
		{
			Xf.Add(FTransform(FQuat::Identity, FVector(0, 0, -100000.f), FVector::ZeroVector));
		}
		Xf.SetNum(Touch);
		Meshes[k]->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
		Shown[k] = Live;
	}
}
