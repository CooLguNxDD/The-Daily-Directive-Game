// Seasonal weather (R18), pure: weather profiles and seasons are data; a sector's season and its weather arc are
// rolled from the run seed by the PCG rules, and the state at any moment is a function of (seed, sector, sector
// clock). Every machine computes the same weather from the replicated seed and server clock: nothing else is sent.
#pragma once

#include "CoreMinimal.h"
#include "Rules/DCPcg.h"
#include "DCWeather.generated.h"

/** One weather state the arc blends between. Kept mild: blasts and photos must stay readable in every profile. */
USTRUCT(BlueprintType)
struct FDCWeatherProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float SunScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FLinearColor SunTint = FLinearColor::White;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float SkyScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float FogDensity = 0.012f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FLinearColor FogColor = FLinearColor(0.75f, 0.55f, 0.38f);
	/** Wind (cm/s) pushing smoke, dust and precipitation. */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FVector Wind = FVector::ZeroVector;
	/** Precipitation puffs per second around the camera, and which puff kind (Rain, Snow, Ash; None = dry). */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FName Precipitation;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float PrecipitationRate = 0.f;
	/** Cloud layer: share of the cloud band shown (0 none, 1 every cloud) and its shade (0 paper light, 1 storm). */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float CloudCover = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float CloudShade = 0.f;
	/** Surface wetness 0..1: up-facing voxels darken and gloss (rain); the blend dries them as the arc moves on. */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float Wetness = 0.f;
	// The weather post layer (R19): added to the stack with weight 1.
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float Exposure = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float Saturation = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float Contrast = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FLinearColor Gain = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FDCWeatherKey
{
	GENERATED_BODY()
	/** 0..1 of the sector's time. */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float At = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FName Profile;
};

USTRUCT(BlueprintType)
struct FDCWeatherArc
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float Weight = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") TArray<FDCWeatherKey> Keys;
};

/** A season: its arcs (one is rolled per sector) and how much snow lies on roofs and streets at the start. */
USTRUCT(BlueprintType)
struct FDCSeasonDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Weather") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") TArray<FDCWeatherArc> Arcs;
	/** 0..1 of exposed roof and street tops that carry a layer of snow voxels. */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") float SnowCover = 0.f;
};

/** The blended weather at one moment. */
struct EPIC_AI_MEGA_JAM2_API FDCWeatherState
{
	FName Season;
	FName Arc;
	FDCWeatherProfile Blend;
	/** Profiles either side of now and how far between them (for logs and tests). */
	FName From;
	FName To;
	float Alpha = 0.f;
};

namespace DCWeather
{
	/** The sector's season: the rule set's slot "Season" rolled with the sector seed. */
	EPIC_AI_MEGA_JAM2_API const FDCSeasonDef* SeasonFor(const TArray<FDCSeasonDef>& Seasons, const FDCPcgRuleSet* Rules, int32 SectorSeed);
	/** The sector's arc, rolled by weight. */
	EPIC_AI_MEGA_JAM2_API const FDCWeatherArc* ArcFor(const FDCSeasonDef& Season, int32 SectorSeed);
	/** The blended weather at Fraction (0..1) of the sector: smoothstep between the keys either side. */
	EPIC_AI_MEGA_JAM2_API FDCWeatherState At(const TArray<FDCWeatherProfile>& Profiles, const FDCSeasonDef& Season, const FDCWeatherArc& Arc, float Fraction);
	EPIC_AI_MEGA_JAM2_API FDCWeatherProfile Lerp(const FDCWeatherProfile& A, const FDCWeatherProfile& B, float T);
}
