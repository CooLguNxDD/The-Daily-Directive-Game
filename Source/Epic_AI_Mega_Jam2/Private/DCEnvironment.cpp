#include "DCEnvironment.h"

#include "DCVoxelCity.h"
#include "DCVoxelFx.h"
#include "Camera/PlayerCameraManager.h"
#include "DCCharacter.h"
#include "HAL/IConsoleManager.h"
#include "Rules/DCPostStack.h"
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
	// Voxel city: the slab stays as an invisible floor at z = 0 (the terrain's collision cooks asynchronously, so a pawn
	// spawned on the first frame must not fall the ground layers' depth); the voxel terrain draws the street.
	const bool bVoxel = ADCVoxelCity::IsEnabled();
	Ground->SetWorldLocation(FVector(0.f, 0.f, -50.f));
	Ground->SetVisibility(!bVoxel);
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
	ApplyPreset();
	ApplyPost(ComposeLook(false, false), S);

	FActorSpawnParameters Params;
	Params.Owner = this;
	DebrisPool = GetWorld()->SpawnActor<ADCDebrisPool>(ADCDebrisPool::StaticClass(), FTransform::Identity, Params);
}

void ADCEnvironment::ApplyPreset()
{
	const UDCTuning& T = UDCTuning::Get();
	FString Wanted = T.PostPreset.ToString();
	FParse::Value(FCommandLine::Get(), TEXT("dcpost="), Wanted);
	const FDCPostPreset* Preset = T.PostPresets.FindByPredicate([&Wanted](const FDCPostPreset& P) { return P.Id.ToString() == Wanted; });
	if (!Preset)
	{
		return;
	}
	PostQuality = Preset->Quality;
	for (const FString& Var : Preset->ConsoleVariables)
	{
		FString Name, Value;
		if (Var.Split(TEXT("="), &Name, &Value))
		{
			if (IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(*Name))
			{
				Cvar->Set(*Value, ECVF_SetByGameSetting);
			}
		}
	}
	if (IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		Sp->Set(Preset->ScreenPercentage, ECVF_SetByGameSetting);
	}
	UE_LOG(LogTemp, Display, TEXT("DCPOST preset %s quality=%d screen=%.0f%%"), *Preset->Id.ToString(), PostQuality, Preset->ScreenPercentage);
}

FDCPostResult ADCEnvironment::ComposeLook(bool bPhoto, bool bSpotlit) const
{
	const UDCTuning& T = UDCTuning::Get();
	// The weather layer (R18) is the blended profile's grade, always on, in the stack at the Weather row's priority.
	TArray<FDCPostLayer> Stack = T.PostLayers;
	if (FDCPostLayer* Row = Stack.FindByPredicate([](const FDCPostLayer& L) { return L.Id == TEXT("Weather"); }))
	{
		Row->DefaultWeight = 1.f;
		Row->Exposure = Weather.Blend.Exposure;
		Row->Saturation = Weather.Blend.Saturation;
		Row->Contrast = Weather.Blend.Contrast;
		Row->Gain = Weather.Blend.Gain;
	}
	if (!bPhoto)
	{
		return DCPostStack::Compose(Stack, PostWeights, PostQuality);
	}
	// The photo keeps the scene's grade and sector look, none of the screen-only layers, a fraction of the bloom
	// spike and a stop-down while the flash is on screen: the target stays readable at the peak (R5).
	const FDCCameraTuning& C = T.Camera;
	TMap<FName, float> W = PostWeights;
	W.Add(TEXT("Viewfinder"), 0.f);
	W.Add(TEXT("Damage"), 0.f);
	W.Add(TEXT("FlashKick"), 0.f);
	W.Add(TEXT("Flash"), FlashPeak * FlashAlpha * C.PhotoBloomFraction);
	W.Add(TEXT("Photo"), 1.f);
	W.Add(TEXT("PhotoFlash"), FlashAlpha * C.PhotoFlashStopDown);
	W.Add(TEXT("Spotlit"), bSpotlit ? 1.f : 0.f);
	return DCPostStack::Compose(Stack, W, PostQuality);
}

void ADCEnvironment::ApplyPost(const FDCPostResult& L, FPostProcessSettings& S)
{
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = L.Bloom;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = L.Exposure;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.f, 1.f, 1.f, L.Saturation);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.f, 1.f, 1.f, L.Contrast);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(L.Gain.R, L.Gain.G, L.Gain.B, 1.f);
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = L.Vignette;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = L.Grain;
}

void ADCEnvironment::UpdatePostWeights()
{
	// Code sets weights by id; what each layer does is data.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	SetPostWeight(TEXT("Flash"), FlashPeak * FlashAlpha);
	SetPostWeight(TEXT("FlashKick"), FlashKick * FlashAlpha);
	for (int32 i = 0; i < 3; ++i)
	{
		SetPostWeight(*FString::Printf(TEXT("Sector%d"), i), GS && GS->SectorIndex == i ? 1.f : 0.f);
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
	SetPostWeight(TEXT("Viewfinder"), Me && Me->IsCameraRaised() && Me->GetSlot() == EDCSlot::Camera ? 1.f : 0.f);
	const float SinceHurt = Me ? GetWorld()->GetRealTimeSeconds() - Me->Feedback().HurtTime : 100.f;
	SetPostWeight(TEXT("Damage"), FMath::Clamp(1.f - SinceHurt / 0.6f, 0.f, 1.f));
}

void ADCEnvironment::Flash(float BloomPeak, float ExposureKick)
{
	FlashPeak = FMath::Max(FlashPeak * FlashAlpha, BloomPeak);
	FlashKick = ExposureKick;
	FlashStartReal = GetWorld()->GetRealTimeSeconds();
}

float ADCEnvironment::GetPhotoBloom(float Fraction) const
{
	return ComposeLook(true, false).Bloom;
}

void ADCEnvironment::UpdateWeather(float DeltaSeconds)
{
	// Seasonal weather (R18): a pure function of the run seed, the sector and the server clock, so every machine
	// shows the same sky without sending any weather over the wire.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const FDCLookTuning& Look = UDCTuning::Get().Look;
	FDCWeatherState Now;
	Now.Blend.FogDensity = Look.FogDensity;
	Now.Blend.FogColor = Look.FogColor;
	if (GS && GS->Phase == EDCRunPhase::Sector && GS->SectorEndTime > GS->SectorStartTime)
	{
		const float Fraction = (GS->Now() - GS->SectorStartTime) / (GS->SectorEndTime - GS->SectorStartTime);
		const FDCWeatherState Rolled = ADCVoxelCity::WeatherAt(GS->RunSeed, GS->SectorIndex, Fraction);
		Now = Rolled.Arc.IsNone() ? Now : Rolled;
	}
	if (Now.From != Weather.From || Now.To != Weather.To)
	{
		UE_LOG(LogTemp, Display, TEXT("DCWEATHER sector=%d season=%s arc=%s %s -> %s"), GS ? GS->SectorIndex : -1, *Now.Season.ToString(), *Now.Arc.ToString(), *Now.From.ToString(), *Now.To.ToString());
	}
	Weather = Now;
	ADCVoxelCity* City = ADCVoxelCity::Get(this);
	ADCVoxelFx* Fx = City ? City->GetFx() : nullptr;
	if (!Fx)
	{
		return;
	}
	Fx->SetWind(Weather.Blend.Wind);
	// Precipitation falls in a column around the local camera (cosmetic and local).
	PrecipitationCarry += Weather.Blend.PrecipitationRate * DeltaSeconds;
	const int32 Count = FMath::FloorToInt32(PrecipitationCarry);
	PrecipitationCarry -= Count;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (Count > 0 && PC && PC->PlayerCameraManager && !Weather.Blend.Precipitation.IsNone())
	{
		Fx->SpawnKind(Weather.Blend.Precipitation, PC->PlayerCameraManager->GetCameraLocation() + FVector(0, 0, 900.f), 3200.f, Count);
	}
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
	float Light = 1.f;
	FLinearColor MutatorFog = FLinearColor::Transparent;
	if (GS)
	{
		for (const FDCUpgradeDef* Mutator : GS->ActiveMutators())
		{
			Light *= Mutator->LightScale;
			MutatorFog = Mutator->bOverrideFog ? Mutator->FogColor : MutatorFog;
		}
	}
	UpdateWeather(DeltaSeconds);
	const FDCWeatherProfile& W = Weather.Blend;
	Sun->SetIntensity(Look.SunIntensityLux * Light * W.SunScale);
	Sun->SetLightColor(Look.SunColor * W.SunTint);
	SkyLight->SetIntensity(Light * W.SkyScale);
	Fog->SetFogDensity(W.FogDensity);
	Fog->SetFogInscatteringColor(MutatorFog.A > 0.f ? MutatorFog : W.FogColor);
	UpdatePostWeights();
	ApplyPost(ComposeLook(false, false), Post->Settings);
}
