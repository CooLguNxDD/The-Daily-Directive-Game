#include "DCEnvironment.h"

#include "DCVoxelCity.h"
#include "Voxel/DCVoxelSettings.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCDebrisPool.h"
#include "DCGameState.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DCTuning.h"
#include "Engine/World.h"

ADCEnvironment::ADCEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;

	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(Root);
	Ground->SetMobility(EComponentMobility::Movable); // placed at run time (top face at z = 0); Static refused the move
	Ground->SetCollisionProfileName(TEXT("BlockAll"));

	for (TObjectPtr<UInstancedStaticMeshComponent>* Ism : {&Sidewalks, &Dashes})
	{
		*Ism = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Ism == &Sidewalks ? TEXT("Sidewalks") : TEXT("Dashes"));
		(*Ism)->SetupAttachment(Root);
		(*Ism)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		(*Ism)->SetCastShadow(false);
	}
}

void ADCEnvironment::BuildStreets()
{
	const FDCCityTuning& City = UDCTuning::Get().City;
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	Sidewalks->SetStaticMesh(DCAssets::Cube());
	Dashes->SetStaticMesh(DCAssets::Cube());
	Sidewalks->SetMaterial(0, DCAssets::MakeFlat(this, Look.Ground));
	Dashes->SetMaterial(0, DCAssets::MakeFlat(this, FLinearColor(0.9f, 0.82f, 0.6f), 0.3f));
	const float Pitch = City.LotSizeCm + City.StreetWidthCm;
	const float OffX = (City.GridX - 1) * Pitch * 0.5f;
	const float OffY = (City.GridY - 1) * Pitch * 0.5f;
	// One raised, kerbed block per lot, plazas included.
	const float Block = (City.LotSizeCm + City.StreetWidthCm * 0.3f) / 100.f;
	for (int32 X = 0; X < City.GridX; ++X)
	{
		for (int32 Y = 0; Y < City.GridY; ++Y)
		{
			Sidewalks->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X * Pitch - OffX, Y * Pitch - OffY, 2.f), FVector(Block, Block, 0.06f)));
		}
	}
	// Dashed centre lines down every street, both directions.
	const float Span = FMath::Max(City.GridX, City.GridY) * Pitch;
	constexpr float DashCm = 300.f;
	constexpr float GapCm = 350.f;
	for (int32 Line = 0; Line <= FMath::Max(City.GridX, City.GridY); ++Line)
	{
		const float Along = Line * Pitch - FMath::Max(OffX, OffY) - Pitch * 0.5f;
		for (float T = -Span * 0.5f; T < Span * 0.5f; T += DashCm + GapCm)
		{
			Dashes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(T, Along, 1.f), FVector(DashCm / 100.f, 0.25f, 0.02f)));
			Dashes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Along, T, 1.f), FVector(0.25f, DashCm / 100.f, 0.02f)));
		}
	}
}

void ADCEnvironment::BeginPlay()
{
	Super::BeginPlay();
	const FDCLookTuning& Look = UDCTuning::Get().Look;

	Sun->SetWorldRotation(Look.SunRotation);
	Sun->SetIntensity(Look.SunIntensityLux);
	Sun->SetLightColor(Look.SunColor);

	Fog->SetFogDensity(Look.FogDensity);
	Fog->SetFogInscatteringColor(Look.FogColor);

	// Ground slab: top face at z = 0, scaled from the 100 cm engine cube.
	Ground->SetStaticMesh(DCAssets::Cube());
	const float Size = Look.GroundSizeCm / 100.f;
	Ground->SetWorldScale3D(FVector(Size, Size, 1.f));
	// Voxel city: the terrain grid carries streets, lots and kerbs from z = 0 down; the slab sinks below it and only
	// catches anything that falls through a crater.
	const bool bVoxel = ADCVoxelCity::IsEnabled();
	const float Below = bVoxel ? UDCVoxelSettings::Get().Tuning.GroundLayers * UDCVoxelSettings::Get().Tuning.TerrainVoxelCm + 20.f : 0.f;
	Ground->SetWorldLocation(FVector(0.f, 0.f, -50.f - Below));
	Ground->SetMaterial(0, DCAssets::MakeFlat(this, Look.Street));
	if (!bVoxel)
	{
		BuildStreets();
	}
	UE_LOG(LogTemp, Display, TEXT("DCENV streets: sidewalks=%d dashes=%d registered=%d visible=%d"), Sidewalks->GetInstanceCount(), Dashes->GetInstanceCount(),
		Sidewalks->IsRegistered() ? 1 : 0, Sidewalks->IsVisible() ? 1 : 0);

	FPostProcessSettings& S = Post->Settings;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.f, 1.f, 1.f, Look.Saturation);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.f, 1.f, 1.f, Look.Contrast);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(Look.ColorGain.R, Look.ColorGain.G, Look.ColorGain.B, 1.f);
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = Look.FilmGrain;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	ApplyLook(Look.BaseBloom, Look.ExposureBias);

	FActorSpawnParameters Params;
	Params.Owner = this;
	DebrisPool = GetWorld()->SpawnActor<ADCDebrisPool>(ADCDebrisPool::StaticClass(), FTransform::Identity, Params);
}

void ADCEnvironment::ApplyLook(float Bloom, float ExposureBias)
{
	FPostProcessSettings& S = Post->Settings;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Bloom;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = ExposureBias;
}

void ADCEnvironment::Flash(float BloomPeak, float ExposureKick)
{
	FlashPeak = FMath::Max(FlashPeak * FlashAlpha, BloomPeak);
	FlashKick = ExposureKick;
	FlashStartReal = GetWorld()->GetRealTimeSeconds();
}

float ADCEnvironment::GetPhotoBloom(float Fraction) const
{
	return UDCTuning::Get().Look.BaseBloom + FlashPeak * FlashAlpha * Fraction;
}

void ADCEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	const double Elapsed = GetWorld()->GetRealTimeSeconds() - FlashStartReal;
	const float T = FMath::Clamp(static_cast<float>(Elapsed) / FMath::Max(Look.FlashDecaySeconds, 0.01f), 0.f, 1.f);
	// Instant peak, fast fall, long tail: readable as "the bloom peak" then "the decay window".
	FlashAlpha = FMath::Pow(1.f - T, 2.5f);

	// Mutator look overrides (Solar Eclipse: the sky goes dark so every blast blazes against it), from data.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const int32 MutatorCount = GS ? GS->Mutators.Num() : 0;
	if (MutatorCount != AppliedMutatorCount)
	{
		AppliedMutatorCount = MutatorCount;
		float Light = 1.f;
		FLinearColor FogColor = Look.FogColor;
		for (const FDCUpgradeDef* Mutator : GS->ActiveMutators())
		{
			Light *= Mutator->LightScale;
			FogColor = Mutator->bOverrideFog ? Mutator->FogColor : FogColor;
		}
		Sun->SetIntensity(Look.SunIntensityLux * Light);
		SkyLight->SetIntensity(Light);
		Fog->SetFogInscatteringColor(FogColor);
	}
	ApplyLook(Look.BaseBloom + FlashPeak * FlashAlpha, Look.ExposureBias + FlashKick * FlashAlpha);
}
