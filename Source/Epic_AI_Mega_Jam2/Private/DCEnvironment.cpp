#include "DCEnvironment.h"
#include "DCSave.h"

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
#include "Materials/MaterialInstanceDynamic.h"

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

	Backdrop = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(Root);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->SetCastShadow(false);
	Clouds = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);
	Clouds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Clouds->SetCastShadow(false);
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
	DayBlend = Look.DayBlend;
	FParse::Value(FCommandLine::Get(), TEXT("dcdaypose="), DayBlend);
	DayBlend = FMath::Clamp(DayBlend, 0.f, 1.f);

	Sun->SetWorldRotation(FMath::Lerp(Look.SunRotation, Look.DaySunRotation, DayBlend));
	Sun->SetIntensity(Look.SunIntensityLux);
	// Light shafts through the towers (Saltline: shafts sell scale), the cheap screen-space kind.
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = Look.LightShaftBloom;
	Sun->BloomThreshold = 0.35f;
	Sun->SetLightShaftOverrideDirection(FVector::ZeroVector);
	Sun->SetLightColor(Look.SunColor);

	Fog->SetFogDensity(Look.FogDensity);
	Fog->SetFogInscatteringColor(Look.FogColor);

	// Poster sky: warm, flat Rayleigh in a tall layer so the zenith stays cream (CP3 vision: it went charcoal).
	Atmosphere->SetRayleighScattering(FMath::Lerp(Look.SkyRayleighColor, Look.DaySkyRayleighColor, DayBlend));
	Atmosphere->SetRayleighScatteringScale(Look.SkyRayleighScale);
	Atmosphere->SetRayleighExponentialDistribution(Look.SkyRayleighHeightKm);
	Atmosphere->SetMieScatteringScale(Look.SkyMieScale);
	Atmosphere->SetMieExponentialDistribution(Look.SkyMieHeightKm);
	Atmosphere->SetMieScattering(Look.SkyMieAlbedo);

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
	if (UMaterialInterface* Ink = Cast<UMaterialInterface>(UDCTuning::Get().Assets.InkOutlineMaterial.TryLoad()))
	{
		const FDCLookTuning& L = UDCTuning::Get().Look;
		OutlineMID = UMaterialInstanceDynamic::Create(Ink, this);
		OutlineMID->SetScalarParameterValue(TEXT("Thickness"), L.OutlineThicknessPx);
		OutlineMID->SetScalarParameterValue(TEXT("DepthThreshold"), L.OutlineDepthThreshold);
		OutlineMID->SetScalarParameterValue(TEXT("NormalThreshold"), L.OutlineNormalThreshold);
		OutlineMID->SetScalarParameterValue(TEXT("FadeDistance"), L.OutlineFadeCm);
		OutlineMID->SetVectorParameterValue(TEXT("InkColor"), L.Enemy); // ink: the darkest tone of the look
	}
	const FDCPostResult Composed = ComposeLook(false, false);
	ApplyPost(Composed, S);
	ApplyOutline(Composed.Outline);

	FActorSpawnParameters Params;
	Params.Owner = this;
	DebrisPool = GetWorld()->SpawnActor<ADCDebrisPool>(ADCDebrisPool::StaticClass(), FTransform::Identity, Params);
}

void ADCEnvironment::ApplyPreset()
{
	const UDCTuning& T = UDCTuning::Get();
	FString Wanted = T.PostPreset.ToString();
	if (const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		// The player's saved quality (R24) picks the preset with that quality level.
		if (const FDCPostPreset* Saved = T.PostPresets.FindByPredicate([Save](const FDCPostPreset& P) { return P.Quality == Save->Settings.Quality; }))
		{
			Wanted = Saved->Id.ToString();
		}
	}
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
	for (const FDCPostLayer& Row : Stack)
	{
		if (Row.bScreenOnly)
		{
			W.Add(Row.Id, 0.f); // marked on the layer row: the print never carries the player's screen layers
		}
	}
	W.Add(TEXT("Flash"), FlashPeak * FlashAlpha / FMath::Max(T.Look.MaxBloomPeak, 1.f) * C.PhotoBloomFraction);
	W.Add(TEXT("Photo"), 1.f);
	W.Add(TEXT("PhotoFlash"), FlashAlpha * C.PhotoFlashStopDown);
	W.Add(TEXT("Spotlit"), bSpotlit ? 1.f : 0.f);
	return DCPostStack::Compose(Stack, W, PostQuality);
}

void ADCEnvironment::ApplyPost(const FDCPostResult& L, FPostProcessSettings& S)
{
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = L.Bloom;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = L.BloomThreshold > 0.f ? L.BloomThreshold : -1.f; // <= 0: no threshold
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
	// The flash weight is 0..1 of the hottest stratagem's peak; the Flash row says how much bloom that is.
	SetPostWeight(TEXT("Flash"), FlashPeak * FlashAlpha / FMath::Max(UDCTuning::Get().Look.MaxBloomPeak, 1.f));
	SetPostWeight(TEXT("FlashKick"), FlashKick * FlashAlpha);
	for (int32 i = 0; i < 3; ++i)
	{
		SetPostWeight(*FString::Printf(TEXT("Sector%d"), i), GS && GS->SectorIndex == i ? 1.f : 0.f);
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const ADCCharacter* Me = PC ? Cast<ADCCharacter>(PC->GetPawn()) : nullptr;
	SetPostWeight(TEXT("Viewfinder"), Me && Me->IsCameraRaised() && Me->GetSlot() == EDCSlot::Camera ? 1.f : 0.f);
	const float SinceHurt = Me ? GetWorld()->GetRealTimeSeconds() - Me->Feedback().HurtTime : 100.f;
	SetPostWeight(TEXT("Damage"), Me && Me->IsDowned() ? 0.f : FMath::Clamp(1.f - SinceHurt / 0.6f, 0.f, 1.f)); // downed: the LastFrame look only
	SetPostWeight(TEXT("LastFrame"), Me && Me->HasLastFrame() ? 1.f : 0.f); // the dying player's own view only
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

void ADCEnvironment::BuildBackdrop(const ADCVoxelCity& City, int32 RunSeed, int32 SectorIndex)
{
	// Seeded per sector, the same on every machine (cosmetic only, no collision).
	const FDCLookTuning& L = UDCTuning::Get().Look;
	Backdrop->ClearInstances();
	for (UInstancedStaticMeshComponent* Lights : BackdropLights)
	{
		Lights->DestroyComponent();
	}
	BackdropLights.Reset();
	float TotalWeight = 0.f;
	for (int32 i = 0; i < L.BackdropBandColors.Num(); ++i)
	{
		const float Wt = L.BackdropBandWeights.IsValidIndex(i) ? L.BackdropBandWeights[i] : 1.f;
		TotalWeight += Wt;
		UInstancedStaticMeshComponent* Lights = NewObject<UInstancedStaticMeshComponent>(this);
		Lights->SetupAttachment(GetRootComponent());
		Lights->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lights->SetCastShadow(false);
		Lights->RegisterComponent();
		Lights->SetStaticMesh(DCAssets::Cube());
		const FLinearColor& Tone = L.BackdropBandColors[i];
		Lights->SetMaterial(0, DCAssets::MakeFlatUnlit(this, FLinearColor(Tone.R, Tone.G, Tone.B), Tone.A)); // alpha = glow
		BackdropLights.Add(Lights);
	}
	Backdrop->SetStaticMesh(DCAssets::Cube());
	Backdrop->SetMaterial(0, DCAssets::MakeFlat(this, L.BackdropColor));
	const float Half = FMath::Abs(static_cast<float>(City.GetSector().Terrain.OriginCm.X));
	FRandomStream Rng(HashCombine(GetTypeHash(RunSeed), GetTypeHash(SectorIndex * 7919 + 13)));
	for (int32 i = 0; i < L.BackdropTowers; ++i)
	{
		const float A = 2.f * PI * (i + Rng.FRand() * 0.6f) / FMath::Max(L.BackdropTowers, 1);
		const float R = Half + L.BackdropGapCm + Rng.FRand() * L.BackdropDepthCm;
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		// The sector is a square: push each tower out to clear its corners.
		const float Square = Half / FMath::Max(FMath::Abs(Dir.X), FMath::Abs(Dir.Y));
		const FVector2D At = Dir * (R - Half + Square);
		const float W = Rng.FRandRange(L.BackdropWidthCm.X, L.BackdropWidthCm.Y);
		const float D = Rng.FRandRange(L.BackdropWidthCm.X, L.BackdropWidthCm.Y);
		const float H = Rng.FRandRange(L.BackdropHeightCm.X, L.BackdropHeightCm.Y);
		const FRotator Yaw(0.f, Rng.FRandRange(0.f, 90.f), 0.f);
		Backdrop->AddInstance(FTransform(Yaw, FVector(At.X, At.Y, H * 0.5f), FVector(W / 100.f, D / 100.f, H / 100.f)));
		// Lit floor bands wrap the tower just outside its faces: a tone per band, picked by weight (seeded).
		const int32 Bands = L.BackdropBandColors.IsEmpty() ? 0 : Rng.RandRange(L.BackdropBands.X, FMath::Max(L.BackdropBands.X, L.BackdropBands.Y));
		for (int32 b = 0; b < Bands && !BackdropLights.IsEmpty(); ++b)
		{
			float Pick = Rng.FRand() * TotalWeight;
			int32 Tone = 0;
			while (Tone < BackdropLights.Num() - 1 && Pick > (L.BackdropBandWeights.IsValidIndex(Tone) ? L.BackdropBandWeights[Tone] : 1.f))
			{
				Pick -= L.BackdropBandWeights.IsValidIndex(Tone) ? L.BackdropBandWeights[Tone] : 1.f;
				++Tone;
			}
			const float Z = Rng.FRandRange(0.15f, 0.92f) * H;
			BackdropLights[Tone]->AddInstance(FTransform(Yaw, FVector(At.X, At.Y, Z), FVector(W * 1.01f / 100.f, D * 1.01f / 100.f, L.BackdropBandCm / 100.f)));
		}
	}
}

void ADCEnvironment::BuildClouds(int32 RunSeed, int32 SectorIndex)
{
	// Seeded per sector like the skyline: the same banks on every machine (cosmetic, no collision).
	const FDCLookTuning& L = UDCTuning::Get().Look;
	Clouds->ClearInstances();
	Clouds->SetStaticMesh(DCAssets::Cube());
	if (!CloudMID)
	{
		CloudMID = DCAssets::MakeFlatUnlit(this, L.CloudLight, L.CloudGlow);
	}
	Clouds->SetMaterial(0, CloudMID);
	CloudBanks.Reset();
	CloudSlabs.Reset();
	FRandomStream Rng(HashCombine(GetTypeHash(RunSeed), GetTypeHash(SectorIndex * 6151 + 29)));
	for (int32 b = 0; b < L.CloudBanks; ++b)
	{
		FCloudBank Bank;
		Bank.Angle = 360.f * (b + Rng.FRand() * 0.7f) / FMath::Max(L.CloudBanks, 1);
		Bank.Radius = Rng.FRandRange(L.CloudRingCm.X, L.CloudRingCm.Y);
		Bank.Height = Rng.FRandRange(L.CloudHeightCm.X, L.CloudHeightCm.Y);
		Bank.Threshold = Rng.FRand(); // shown once CloudCover passes it: more cover, more banks, same order everywhere
		CloudBanks.Add(Bank);
		// A bank is a stepped stack: a long base slab, shorter ones on top, offset along the ring (poster cut-paper).
		const int32 Slabs = Rng.RandRange(L.CloudSlabs.X, FMath::Max(L.CloudSlabs.X, L.CloudSlabs.Y));
		float Length = Rng.FRandRange(L.CloudSlabCm.X, L.CloudSlabCm.Y) * 1.6f;
		for (int32 s = 0; s < Slabs; ++s)
		{
			FCloudSlab Slab;
			Slab.Bank = b;
			const float Z = (s / 2) * L.CloudSlabThickCm;
			Slab.Offset = FVector(Rng.FRandRange(-0.25f, 0.25f) * Length, Rng.FRandRange(-0.3f, 0.3f) * Length, Z);
			Slab.Scale = FVector(Rng.FRandRange(0.35f, 0.6f) * Length, Length, L.CloudSlabThickCm) / 100.f;
			CloudSlabs.Add(Slab);
			Clouds->AddInstance(FTransform::Identity);
			Length *= Rng.FRandRange(0.55f, 0.85f);
		}
	}
	CloudXforms.SetNum(CloudSlabs.Num());
}

void ADCEnvironment::UpdateClouds(float DeltaSeconds)
{
	if (CloudSlabs.IsEmpty())
	{
		return;
	}
	const FDCLookTuning& L = UDCTuning::Get().Look;
	const FDCWeatherProfile& W = Weather.Blend;
	CloudOrbitDeg = FMath::Fmod(CloudOrbitDeg + W.Wind.Size2D() / 100.f * L.CloudOrbitDegPerWind * DeltaSeconds, 360.f);
	if (CloudMID)
	{
		CloudMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(L.CloudLight, L.CloudDark, FMath::Clamp(W.CloudShade, 0.f, 1.f)));
	}
	for (int32 i = 0; i < CloudSlabs.Num(); ++i)
	{
		const FCloudSlab& S = CloudSlabs[i];
		const FCloudBank& B = CloudBanks[S.Bank];
		// Each bank grows in over a 0.15 cover band past its threshold, so cover changes never pop banks in.
		const float Grow = FMath::Clamp((W.CloudCover - B.Threshold) / 0.15f, 0.f, 1.f);
		const float A = FMath::DegreesToRadians(B.Angle + CloudOrbitDeg);
		const FVector Center(FMath::Cos(A) * B.Radius, FMath::Sin(A) * B.Radius, B.Height);
		const FRotator Yaw(0.f, FMath::RadiansToDegrees(A), 0.f); // the long side runs along the ring
		const FVector Scale = Grow > 0.f ? S.Scale * FVector(Grow, Grow, FMath::Max(Grow, 0.2f)) : FVector(0.001f);
		CloudXforms[i] = FTransform(Yaw, Center + Yaw.RotateVector(S.Offset), Scale);
	}
	Clouds->BatchUpdateInstancesTransforms(0, CloudXforms, false, true);
}

void ADCEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const ADCGameState* BGS = GetWorld()->GetGameState<ADCGameState>())
	{
		const ADCVoxelCity* City = ADCVoxelCity::IsEnabled() ? ADCVoxelCity::Get(this) : nullptr;
		if (City && City->GetBuiltSector() != INDEX_NONE && City->GetBuildId() != BackdropBuild)
		{
			BackdropBuild = City->GetBuildId();
			BuildBackdrop(*City, BGS->RunSeed, City->GetBuiltSector());
			BuildClouds(BGS->RunSeed, City->GetBuiltSector());
		}
	}
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
	UpdateClouds(DeltaSeconds);
	const FDCWeatherProfile& W = Weather.Blend;
	ADCVoxelCity* WetCity = ADCVoxelCity::IsEnabled() ? ADCVoxelCity::Get(this) : nullptr;
	if (WetCity && FMath::Abs(W.Wetness - AppliedWetness) > 0.01f)
	{
		AppliedWetness = W.Wetness;
		WetCity->SetWetness(W.Wetness);
	}
	// Night look blended toward the day end by DayBlend (the day-night cycle's seam; 0 = the noir board).
	const float Day = DayBlend;
	Sun->SetIntensity(FMath::Lerp(Look.SunIntensityLux, Look.DaySunLux, Day) * Light * W.SunScale);
	Sun->SetLightColor(FMath::Lerp(Look.SunColor, Look.DaySunColor, Day) * W.SunTint);
	SkyLight->SetIntensity(Light * W.SkyScale * FMath::Lerp(Look.SkyFill, Look.DaySkyFill, Day));
	// The poster sky holds cream to the zenith under any weather: dimming the sun would grey the sky with it, so the
	// sky luminance makes up for the weather's sun scale (V4.3). Mutators (eclipse) still darken it through Light.
	const float SkyKeep = FMath::Lerp(Look.SkyLuminance, Look.DaySkyLuminance, Day) * Light / FMath::Max(W.SunScale, Look.SkyMinSunScale);
	Atmosphere->SetSkyLuminanceFactor(FLinearColor(SkyKeep, SkyKeep, SkyKeep));
	Fog->SetFogDensity(W.FogDensity);
	Fog->SetFogInscatteringColor(MutatorFog.A > 0.f ? MutatorFog : FMath::Lerp(W.FogColor, Look.DayFogColor, Day));
	UpdatePostWeights();
	const FDCPostResult Composed = ComposeLook(false, false);
	ApplyPost(Composed, Post->Settings);
	Post->Settings.AutoExposureBias += Look.DayExposure * Day;
	ApplyOutline(Composed.Outline);
}

void ADCEnvironment::ApplyOutline(float Strength)
{
	if (!OutlineMID)
	{
		return;
	}
	OutlineMID->SetScalarParameterValue(TEXT("Strength"), Strength);
	TArray<FWeightedBlendable>& Blendables = Post->Settings.WeightedBlendables.Array;
	FWeightedBlendable* Mine = Blendables.FindByPredicate([this](const FWeightedBlendable& B) { return B.Object == OutlineMID; });
	if (!Mine)
	{
		Mine = &Blendables.Add_GetRef(FWeightedBlendable(0.f, OutlineMID));
	}
	Mine->Weight = Strength > 0.f ? 1.f : 0.f;
}
