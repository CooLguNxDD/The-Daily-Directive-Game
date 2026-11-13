#include "DCBuilding.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCDebrisPool.h"
#include "DCEnvironment.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** A few fixed shades so debris shares a handful of materials instead of one per piece. */
	constexpr float DebrisShades[] = {0.55f, 0.75f, 1.f};

	UInstancedStaticMeshComponent* MakeIsm(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UInstancedStaticMeshComponent* Ism = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Ism->SetupAttachment(Parent);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetCollisionProfileName(TEXT("BlockAll"));
		return Ism;
	}

	const FLinearColor& PaletteColor(uint8 Index)
	{
		const TArray<FLinearColor>& Colors = UDCTuning::Get().Look.BuildingColors;
		static const FLinearColor Fallback(0.55f, 0.5f, 0.42f);
		return Colors.Num() > 0 ? Colors[Index % Colors.Num()] : Fallback;
	}
}

ADCBuilding::ADCBuilding()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Walls = MakeIsm(this, TEXT("Walls"), RootComponent);
	Windows = MakeIsm(this, TEXT("Windows"), RootComponent);
	Trim = MakeIsm(this, TEXT("Trim"), RootComponent);
	Rubble = MakeIsm(this, TEXT("Rubble"), RootComponent);
	// Rubble appears where players may be standing after they ran in: it never traps a pawn.
	Rubble->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

void ADCBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCBuilding, Spec);
	DOREPLIFETIME(ADCBuilding, bCollapsed);
	DOREPLIFETIME(ADCBuilding, CollapseTime);
	DOREPLIFETIME(ADCBuilding, Epicenter);
	DOREPLIFETIME(ADCBuilding, LotIndex);
}

void ADCBuilding::BeginPlay()
{
	Super::BeginPlay();
	BuildBlocks();
	if (bCollapsed)
	{
		SimulateCollapse(LateJoinAnimates());
	}
}

void ADCBuilding::OnRep_Spec()
{
	bBuilt = false;
	BuildBlocks();
}

void ADCBuilding::BuildBlocks()
{
	if (bBuilt || Spec.Floors <= 0)
	{
		return;
	}
	bBuilt = true;
	UStaticMesh* Cube = DCAssets::Cube();
	for (UInstancedStaticMeshComponent* Ism : {Walls.Get(), Windows.Get(), Trim.Get(), Rubble.Get()})
	{
		Ism->ClearInstances();
		Ism->SetStaticMesh(Cube);
	}
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	const FLinearColor Base = PaletteColor(Spec.ColorIndex);
	Walls->SetMaterial(0, DCAssets::MakeFlat(this, Base));
	Windows->SetMaterial(0, DCAssets::MakeFlat(this, Base * 0.25f, 0.05f));
	Trim->SetMaterial(0, DCAssets::MakeFlat(this, Look.Accent));
	Rubble->SetMaterial(0, DCAssets::MakeFlat(this, Base * 0.6f));

	const float B = Spec.BlockSize;
	const float S = B / 100.f;
	const FVector Origin(-(Spec.SizeX - 1) * B * 0.5f, -(Spec.SizeY - 1) * B * 0.5f, B * 0.5f);
	for (int32 F = 0; F < Spec.Floors; ++F)
	{
		const bool bWindowFloor = (F % 2) == 1 && F < Spec.Floors - 1;
		for (int32 X = 0; X < Spec.SizeX; ++X)
		{
			for (int32 Y = 0; Y < Spec.SizeY; ++Y)
			{
				const bool bEdgeX = X == 0 || X == Spec.SizeX - 1;
				const bool bEdgeY = Y == 0 || Y == Spec.SizeY - 1;
				const bool bPerimeter = bEdgeX || bEdgeY;
				const bool bCorner = bEdgeX && bEdgeY;
				if (!bPerimeter && F != Spec.Floors - 1)
				{
					continue; // hollow core: only the shell and the roof are blocks
				}
				const FVector P = Origin + FVector(X * B, Y * B, F * B);
				const bool bWindow = bWindowFloor && !bCorner && bPerimeter;
				const FVector Scale = bWindow ? FVector(S * 0.94f) : FVector(S);
				(bWindow ? Windows : Walls)->AddInstance(FTransform(FRotator::ZeroRotator, P, Scale));
			}
		}
	}
	// Accent cornice on the roof edge and a plinth slab at street level.
	const FVector Footprint(Spec.SizeX * S, Spec.SizeY * S, 0.f);
	Trim->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, Spec.Floors * B + 0.15f * B), FVector(Footprint.X + 0.2f, Footprint.Y + 0.2f, 0.3f * S)));
	Trim->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, 5.f), FVector(Footprint.X + 1.2f, Footprint.Y + 1.2f, 0.1f)));
}

FVector ADCBuilding::GetCenter() const
{
	const float H = bCollapsed ? Spec.BlockSize : GetHeight();
	return GetActorLocation() + FVector(0, 0, H * 0.5f);
}

void ADCBuilding::GetSamplePoints(TArray<FVector>& Out) const
{
	const FVector C = GetActorLocation();
	const float H = bCollapsed ? Spec.BlockSize : GetHeight();
	const float Hx = Spec.SizeX * Spec.BlockSize * 0.4f;
	const float Hy = Spec.SizeY * Spec.BlockSize * 0.4f;
	Out.Add(C + FVector(0, 0, H * 0.5f));
	Out.Add(C + FVector(0, 0, H * 0.9f));
	Out.Add(C + FVector(Hx, Hy, H * 0.3f));
	Out.Add(C + FVector(-Hx, -Hy, H * 0.3f));
	Out.Add(C + FVector(Hx, -Hy, H * 0.7f));
	Out.Add(C + FVector(-Hx, Hy, H * 0.7f));
}

void ADCBuilding::Collapse(const FVector& InEpicenter)
{
	if (!HasAuthority() || bCollapsed)
	{
		return;
	}
	bCollapsed = true;
	Epicenter = InEpicenter;
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	CollapseTime = GS ? GS->Now() : GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
	SimulateCollapse(true);
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		GM->OnBuildingCollapsed(this);
	}
}

bool ADCBuilding::LateJoinAnimates() const
{
	// Late joiners (more than a few seconds after the collapse) only see the rubble.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	return !GS || GS->Now() - CollapseTime < 3.f;
}

void ADCBuilding::RestageCollapse(const FVector& InEpicenter, bool bAnimate)
{
	if (bCollapsed)
	{
		return;
	}
	bCollapsed = true;
	Epicenter = InEpicenter;
	BuildBlocks();
	SimulateCollapse(bAnimate);
}

void ADCBuilding::OnRep_Collapsed()
{
	if (bCollapsed)
	{
		BuildBlocks();
		SimulateCollapse(LateJoinAnimates());
	}
}

void ADCBuilding::SimulateCollapse(bool bAnimateRequested)
{
	if (bSimulated)
	{
		return;
	}
	bSimulated = true;
	const FDCCityTuning& City = UDCTuning::Get().City;
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCDebrisPool* Pool = GS && GS->GetEnvironment() ? GS->GetEnvironment()->GetDebrisPool() : nullptr;

	const bool bAnimate = Pool && bAnimateRequested;

	TArray<FTransform> Pieces;
	for (UInstancedStaticMeshComponent* Ism : {Walls.Get(), Windows.Get()})
	{
		for (int32 i = 0; i < Ism->GetInstanceCount(); ++i)
		{
			FTransform T;
			Ism->GetInstanceTransform(i, T, true);
			Pieces.Add(T);
		}
		Ism->ClearInstances();
	}
	FRandomStream Rng(Spec.Seed ^ 0x5eed);
	if (bAnimate)
	{
		const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(Pieces.Num(), City.MaxDebrisPerCollapse));
		const float Grow = FMath::Pow(static_cast<float>(Stride), 1.f / 3.f);
		const FVector Center = GetActorLocation();
		const float Height = FMath::Max(GetHeight(), 1.f);
		for (int32 i = 0; i < Pieces.Num(); i += Stride)
		{
			FDCDebrisSpawn D;
			D.Transform = Pieces[i];
			D.Transform.SetScale3D(D.Transform.GetScale3D() * Grow * Rng.FRandRange(0.7f, 1.05f));
			D.Transform.SetRotation(FQuat(FRotator(Rng.FRandRange(-8, 8), Rng.FRandRange(0, 360), Rng.FRandRange(-8, 8))));
			const FVector P = Pieces[i].GetLocation();
			FVector Away = P - FVector(Epicenter.X, Epicenter.Y, P.Z);
			Away = Away.IsNearlyZero() ? (P - Center).GetSafeNormal2D() : Away.GetSafeNormal();
			const float HeightFrac = (P.Z - Center.Z) / Height;
			const float Push = 250.f + 900.f * HeightFrac;
			D.LinearVelocity = Away * Push + FVector(Rng.FRandRange(-150, 150), Rng.FRandRange(-150, 150), Rng.FRandRange(0.f, 500.f));
			D.AngularVelocityDeg = FVector(Rng.FRandRange(-200, 200), Rng.FRandRange(-200, 200), Rng.FRandRange(-200, 200));
			const bool bFire = Rng.FRand() < City.FireFraction;
			D.Color = bFire ? Look.Fire : PaletteColor(Spec.ColorIndex) * DebrisShades[Rng.RandRange(0, UE_ARRAY_COUNT(DebrisShades) - 1)];
			D.Glow = bFire ? 25.f : 0.f;
			D.LifeSeconds = City.DebrisLifeSeconds * Rng.FRandRange(0.7f, 1.2f);
			Pool->Spawn(D);
		}
		DCAudio::PlayAt(this, TEXT("Collapse"), GetActorLocation(), 1.f, Rng.FRandRange(0.85f, 1.1f));
	}

	// The cornice falls too; the plinth (index 1) stays.
	if (Trim->GetInstanceCount() > 0)
	{
		Trim->RemoveInstance(0);
	}
	// Rubble mound where the tower stood.
	const float B = Spec.BlockSize;
	const float S = B / 100.f;
	for (int32 X = 0; X < Spec.SizeX; ++X)
	{
		for (int32 Y = 0; Y < Spec.SizeY; ++Y)
		{
			const FVector P(-(Spec.SizeX - 1) * B * 0.5f + X * B, -(Spec.SizeY - 1) * B * 0.5f + Y * B, 0.f);
			const float H = Rng.FRandRange(0.3f, 1.4f);
			Rubble->AddInstance(FTransform(FRotator(Rng.FRandRange(-12, 12), Rng.FRandRange(0, 90), Rng.FRandRange(-12, 12)),
				P + FVector(0, 0, H * B * 0.4f), FVector(S * 1.1f, S * 1.1f, S * H)));
		}
	}
}
