#include "DCEnvironment.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCDebrisPool.h"
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
	Ground->SetMobility(EComponentMobility::Static);
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
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
	Ground->SetWorldLocation(FVector(0.f, 0.f, -50.f));
	Ground->SetMaterial(0, DCAssets::MakeFlat(this, Look.Street));

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
	ApplyLook(Look.BaseBloom + FlashPeak * FlashAlpha, Look.ExposureBias + FlashKick * FlashAlpha);
}
