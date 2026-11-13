#include "DCSupplyPod.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float FallSeconds = 1.6f;
	constexpr float FallHeightCm = 9000.f;
	constexpr float PickupRadiusCm = 260.f;
	constexpr float PodLifeSeconds = 45.f;
}

ADCSupplyPod::ADCSupplyPod()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	auto Make = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* M = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		M->SetupAttachment(RootComponent);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return M;
	};
	Body = Make(TEXT("Body"));
	Stripe = Make(TEXT("Stripe"));
	Beam = Make(TEXT("Beam"));
	Beam->SetCastShadow(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(0, 0, 200.f));
	Light->SetAttenuationRadius(1500.f);
}

void ADCSupplyPod::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCSupplyPod, LandTime);
	DOREPLIFETIME(ADCSupplyPod, Ground);
}

ADCSupplyPod* ADCSupplyPod::Drop(UWorld* World, const FVector& Where)
{
	const ADCGameState* GS = World->GetGameState<ADCGameState>();
	FHitResult Hit;
	FVector Floor = Where;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DCPodGround), false);
	if (World->LineTraceSingleByChannel(Hit, Where + FVector(0, 0, 300), Where - FVector(0, 0, 5000), ECC_WorldStatic, Params))
	{
		Floor = Hit.ImpactPoint;
	}
	ADCSupplyPod* Pod = World->SpawnActorDeferred<ADCSupplyPod>(ADCSupplyPod::StaticClass(), FTransform(Floor));
	Pod->Ground = Floor;
	Pod->LandTime = (GS ? GS->Now() : World->GetTimeSeconds()) + FallSeconds;
	Pod->FinishSpawning(FTransform(Floor));
	Pod->SetLifeSpan(PodLifeSeconds);
	return Pod;
}

void ADCSupplyPod::BeginPlay()
{
	Super::BeginPlay();
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	Body->SetStaticMesh(DCAssets::Cylinder());
	Body->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.85f, 0.8f, 0.68f)));
	Body->SetRelativeScale3D(FVector(1.3f, 1.3f, 2.f));
	Body->SetRelativeLocation(FVector(0, 0, 100.f));
	Stripe->SetStaticMesh(DCAssets::Cylinder());
	Stripe->SetMaterial(0, DCAssets::MakeFlat(this, Look.Accent, 8.f));
	Stripe->SetRelativeScale3D(FVector(1.35f, 1.35f, 0.3f));
	Stripe->SetRelativeLocation(FVector(0, 0, 150.f));
	Beam->SetStaticMesh(DCAssets::Cylinder());
	Beam->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.2f, 1.f, 0.4f), 20.f));
	Beam->SetRelativeScale3D(FVector(0.15f, 0.15f, 60.f));
	Beam->SetRelativeLocation(FVector(0, 0, 3000.f));
	Light->SetLightColor(FLinearColor(0.2f, 1.f, 0.4f));
	Light->SetIntensity(0.f);
	DCAudio::PlayAt(this, TEXT("PodDrop"), Ground, 1.f);
}

void ADCSupplyPod::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const float Now = GS ? GS->Now() : GetWorld()->GetTimeSeconds();
	const float Left = LandTime - Now;
	if (Left > 0.f)
	{
		const float U = Left / FallSeconds;
		SetActorLocation(FVector(Ground) + FVector(0, 0, FallHeightCm * U * U));
		return;
	}
	if (!bLanded)
	{
		bLanded = true;
		SetActorLocation(Ground);
		DCAudio::PlayAt(this, TEXT("Land"), Ground, 1.f, 0.5f);
		DCAudio::PlayAt(this, TEXT("Stamp"), Ground, 0.8f, 0.8f);
	}
	Light->SetIntensity(60000.f + 40000.f * FMath::Sin(Now * 6.f));
	if (!HasAuthority())
	{
		return;
	}
	const FDCRunTuning& Run = UDCTuning::Get().Run;
	for (ADCCharacter* C : TActorRange<ADCCharacter>(GetWorld()))
	{
		if (!C->IsDowned() && FVector::Dist2D(C->GetActorLocation(), Ground) < PickupRadiusCm)
		{
			C->HostResupply(Run.PodFilm, Run.PodAmmo, Run.PodHealth);
			Destroy();
			return;
		}
	}
}
