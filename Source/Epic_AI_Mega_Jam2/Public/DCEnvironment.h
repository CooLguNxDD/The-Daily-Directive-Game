// Local-only scene dressing spawned on every machine: sun, sky, fog, ground, the unbound post-process volume
// that carries the propaganda look, and the bloom flash a detonation drives. Never replicated.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/DCPostStack.h"
#include "Rules/DCWeather.h"
#include "DCEnvironment.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UStaticMeshComponent;
class ADCDebrisPool;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCEnvironment : public AActor
{
	GENERATED_BODY()

public:
	ADCEnvironment();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Bloom and exposure spike that decays over the look tuning's FlashDecaySeconds (real time). */
	void Flash(float BloomPeak, float ExposureKick);

	/** 0..1 how blown-out the screen is right now (HUD draws a white veil from it). */
	float GetFlashAlpha() const { return FlashAlpha; }

	/** Bloom for a photo taken right now: the base look plus a fraction of the flash. */
	float GetPhotoBloom(float Fraction) const;

	ADCDebrisPool* GetDebrisPool() const { return DebrisPool; }

	/** Post stack (R19): the weight of a layer by id this frame (layers are Look rows in the ini). */
	void SetPostWeight(FName Layer, float Weight) { PostWeights.Add(Layer, Weight); }
	/** The scene's composed look, or the photo's: the scene layers plus the photo layers (exposure kept for photos). */
	FDCPostResult ComposeLook(bool bPhoto, bool bSpotlit) const;
	static void ApplyPost(const FDCPostResult& Look, FPostProcessSettings& Out);
	/** The ink outline blendable on the scene volume, at the composed Outline strength (0 removes it). */
	void ApplyOutline(float Strength);
	int32 GetPostQuality() const { return PostQuality; }
	float GetEffectScale() const { return EffectScale; }
	/** The active preset's EffectScale (1 when there is no environment yet). */
	static float EffectScaleOf(const UObject* WorldContext);
	const FDCWeatherState& GetWeather() const { return Weather; }

	/** Applies the post preset (saved quality, or -dcpost=): scalability console variables and screen percentage. */
	void ApplyPreset();

private:
	void UpdateWeather(float DeltaSeconds);
	void UpdatePostWeights();
	void BuildStreets();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> OutlineMID;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ground;
	/** The street grid read from the city tuning (same on every machine and every sector): kerbed blocks and lane dashes. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> Sidewalks;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> Dashes;
	/** The skyline beyond the sector edge: ink tower silhouettes in a ring (no barren plain past the last street). */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> Backdrop;
	/** Lit floor bands on the skyline towers (R25: warm dim windows, a few magenta and cyan bands), one ISM per tone. */
	UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> BackdropLights;
	int32 BackdropBuild = INDEX_NONE;
	void BuildBackdrop(const class ADCVoxelCity& City, int32 RunSeed, int32 Sector);
	/** Cloud layer (R18): seeded banks per sector, shown and shaded by the weather blend, orbiting with the wind. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UInstancedStaticMeshComponent> Clouds;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> CloudMID;
	struct FCloudSlab { FVector Offset; FVector Scale; int32 Bank; };
	struct FCloudBank { float Angle; float Radius; float Height; float Threshold; };
	TArray<FCloudBank> CloudBanks;
	TArray<FCloudSlab> CloudSlabs;
	TArray<FTransform> CloudXforms;
	float CloudOrbitDeg = 0.f;
	float AppliedWetness = -1.f;
	void BuildClouds(int32 RunSeed, int32 Sector);
	/** R12 sky detail and skyline landmarks (+SkyProps rows, DCSkyProps.cpp). */
	struct FSkyProp { float Angle; float Radius; float Height; float OrbitDegPerSec; float BobCm; float Phase; };
	UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> SkyMeshes;
	TArray<FSkyProp> SkyState;
	void BuildSkyProps(int32 RunSeed, int32 Sector, float HalfCm);
	void UpdateSkyProps(float DeltaSeconds);
	void UpdateClouds(float DeltaSeconds);
	UPROPERTY() TObjectPtr<ADCDebrisPool> DebrisPool;

	float FlashPeak = 0.f;
	float FlashKick = 0.f;
	double FlashStartReal = -100.0;
	float FlashAlpha = 0.f;
	int32 AppliedMutatorCount = 0;
	TMap<FName, float> PostWeights;
	FDCWeatherState Weather;
	float PrecipitationCarry = 0.f;
	float GroundFxCarry = 0.f;
	/** 0 = night, 1 = day (Look.DayBlend, or -dcdaypose=<0..1> for evidence runs). */
	float DayBlend = 0.f;
	/** Day value the sun and sky were last set for (they change only when it moves). */
	float AppliedDay = -1.f;
	int32 PostQuality = 2;
	float EffectScale = 1.f;
};
