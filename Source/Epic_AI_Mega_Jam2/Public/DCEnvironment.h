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
	int32 BackdropBuild = INDEX_NONE;
	void BuildBackdrop(const class ADCVoxelCity& City, int32 RunSeed, int32 Sector);
	UPROPERTY() TObjectPtr<ADCDebrisPool> DebrisPool;

	float FlashPeak = 0.f;
	float FlashKick = 0.f;
	double FlashStartReal = -100.0;
	float FlashAlpha = 0.f;
	int32 AppliedMutatorCount = 0;
	TMap<FName, float> PostWeights;
	FDCWeatherState Weather;
	float PrecipitationCarry = 0.f;
	int32 PostQuality = 2;
};
