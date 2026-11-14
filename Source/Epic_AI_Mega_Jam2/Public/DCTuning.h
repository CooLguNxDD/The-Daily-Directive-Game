// All game tuning in one text-backed settings object: Config/DefaultGame.ini [/Script/Epic_AI_Mega_Jam2.DCTuning].
// Code reads it through UDCTuning::Get(); nothing here holds logic.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "DCTypes.h"
#include "Rules/DCPhotoRules.h"
#include "Rules/DCPressureRules.h"
#include "Rules/DCNewsprint.h"
#include "Rules/DCPostStack.h"
#include "Rules/DCRunRules.h"
#include "Rules/DCWeather.h"
#include "DCTuning.generated.h"

USTRUCT(BlueprintType)
struct FDCMoveTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Move") float WalkSpeed = 450.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float SprintSpeed = 720.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float CrouchSpeed = 220.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float CameraAimSpeed = 260.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float MaxAcceleration = 2600.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float BrakingDeceleration = 3000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float GroundFriction = 10.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float JumpZVelocity = 520.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float AirControl = 0.3f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float GravityScale = 1.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float FootstepIntervalCm = 190.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float MouseSensitivity = 1.0f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float ThirdPersonFOV = 90.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float ArmLength = 260.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") FVector ShoulderOffset = FVector(0.f, 65.f, 65.f);
	UPROPERTY(EditAnywhere, Config, Category = "Move") float MaxHealth = 100.f;
	/** Landing dip as a spring (Saltline, R9): accel = -x * Stiffness - v * Damping; dip capped at LandDipMaxCm. */
	UPROPERTY(EditAnywhere, Config, Category = "Move") float LandDipStiffness = 150.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float LandDipDamping = 17.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float LandDipMaxCm = 20.f;
	/** Landing speed (cm/s) that gives the full dip. */
	UPROPERTY(EditAnywhere, Config, Category = "Move") float LandDipFullSpeed = 1200.f;
	/** Field of view added while sprinting (third person). */
	UPROPERTY(EditAnywhere, Config, Category = "Move") float SprintFOVKick = 6.f;
	/** Downed third-person camera: boom pivot height above the actor (cm) and arm length, clear of the rubble. */
	UPROPERTY(EditAnywhere, Config, Category = "Move") float DownedBoomHeightCm = 260.f;
	UPROPERTY(EditAnywhere, Config, Category = "Move") float DownedArmLength = 520.f;
};

USTRUCT(BlueprintType)
struct FDCRifleTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") int32 MagazineSize = 30;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") int32 StartReserve = 180;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float ShotsPerSecond = 9.f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float Damage = 14.f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float RangeCm = 12000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float ReloadSeconds = 1.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float RecoilPitch = 0.55f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float RecoilYawJitter = 0.18f;
	/** Fraction of accumulated recoil the view recovers per second once the trigger is released. */
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float RecoilRecoverPerSecond = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") float SpreadDegrees = 0.8f;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") int32 LowAmmoWarning = 8;
};

/** A stratagem the player can call. Heavier ones cost Propaganda Points. */
USTRUCT(BlueprintType)
struct FDCStratagemDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Strike") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") FText DisplayName;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") int32 Cost = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float CooldownSeconds = 12.f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float TelegraphSeconds = 3.f;
	/** Radius (cm) that collapses buildings and hurts enemies. */
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float BlastRadius = 1400.f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float Damage = 400.f;
	/** Players inside this radius get hurt too: the blast is dangerous up close. */
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float PlayerHurtRadius = 700.f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float PlayerDamage = 35.f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float BloomPeak = 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float FireballScale = 14.f;
	/** Number of sequential blasts (a barrage) and spacing in seconds. */
	UPROPERTY(EditAnywhere, Config, Category = "Strike") int32 Blasts = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float BlastSpacing = 0.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") float BlastScatterCm = 0.f;
	/** Behaviour id resolved by the strike registry: Laser, SunDrop, Singularity, Decoy. */
	UPROPERTY(EditAnywhere, Config, Category = "Strike") FName Behaviour = TEXT("Laser");
	UPROPERTY(EditAnywhere, Config, Category = "Strike") bool bStartsUnlocked = true;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") FLinearColor BeamColor = FLinearColor(1.f, 0.1f, 0.05f);
};

USTRUCT(BlueprintType)
struct FDCCameraTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Camera") int32 ShotsPerSector = 15;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float WindOnSeconds = 0.7f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float DrawSeconds = 0.45f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float ViewfinderFOVWide = 70.f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float ViewfinderFOVTight = 25.f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float ZoomStep = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float VignetteIntensity = 1.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") int32 PhotoWidth = 640;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") int32 PhotoHeight = 360;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float StampHoldSeconds = 2.6f;
	/** Host: a shot taken while a voxel op is on the worker waits up to this long (real time) for the op to land, so
	 *  the strike's collapse records exist when it is graded (C3.1). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float GradeWaitForOpSeconds = 2.f;
	/** How far the floating tag scan looks (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float TagRangeCm = 9000.f;
	/** Swarmers within this radius rush a photographer who raises the camera (R11). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float AggroRadiusCm = 5000.f;
	/** The photo keeps this fraction of a blast's bloom spike, and none of its exposure kick, so a peak shot reads
	 *  as fire and debris instead of a white frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float PhotoBloomFraction = 0.15f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float PhotoExposureBias = -0.6f;
	/** Extra stops the photo closes down at the full flash (scaled by the flash still on screen). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float PhotoFlashStopDown = 2.f;
	/** Stops a spotlit photo is pushed over (R11: spotlight units blow out exposure). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float SpotlitPhotoStops = 3.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") float SpotlitPhotoBloom = 8.f;
	/** Newsprint treatment of every photo (R5). */
	UPROPERTY(EditAnywhere, Config, Category = "Camera") FDCPrintTuning Print;
};

USTRUCT(BlueprintType)
struct FDCEconomyTuning
{
	GENERATED_BODY()
	/** Propaganda Points per photo score point. */
	UPROPERTY(EditAnywhere, Config, Category = "Economy") float PointsPerScore = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Economy") int32 KillPoints = 5;
	UPROPERTY(EditAnywhere, Config, Category = "Economy") int32 StartPoints = 0;
};

USTRUCT(BlueprintType)
struct FDCCityTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 GridX = 4;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 GridY = 4;
	UPROPERTY(EditAnywhere, Config, Category = "City") float LotSizeCm = 2600.f;
	UPROPERTY(EditAnywhere, Config, Category = "City") float StreetWidthCm = 1600.f;
	UPROPERTY(EditAnywhere, Config, Category = "City") float BlockSizeCm = 300.f;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MinFloors = 6;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MaxFloors = 16;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MinFootprint = 3;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MaxFootprint = 5;
	/** Chance a lot stays empty (plaza). */
	UPROPERTY(EditAnywhere, Config, Category = "City") float EmptyLotChance = 0.2f;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MinBuildings = 8;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MaxBuildings = 15;
	/** Max simulated debris bodies per collapse; the rest become rubble at once. */
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 MaxDebrisPerCollapse = 140;
	UPROPERTY(EditAnywhere, Config, Category = "City") float DebrisLifeSeconds = 7.f;
	UPROPERTY(EditAnywhere, Config, Category = "City") float FireFraction = 0.25f;
	UPROPERTY(EditAnywhere, Config, Category = "City") int32 DebrisPoolSize = 600;
	/** Chance a tower hangs a propaganda banner (red cloth, cream emblem) down one face. */
	UPROPERTY(EditAnywhere, Config, Category = "City") float BannerChance = 0.65f;
	/** A tower a walker shoulders into shakes this long before it gives way (photographable warning). */
	UPROPERTY(EditAnywhere, Config, Category = "City") float ToppleTelegraphSeconds = 1.5f;
	/** Shake amplitude (cm) of a condemned tower at the end of its telegraph. */
	UPROPERTY(EditAnywhere, Config, Category = "City") float CondemnedShakeCm = 18.f;
};

/** Flat propaganda palette and post-process look. */
USTRUCT(BlueprintType)
struct FDCLookTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor Ground = FLinearColor(0.32f, 0.28f, 0.22f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor Street = FLinearColor(0.08f, 0.07f, 0.07f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") TArray<FLinearColor> BuildingColors;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor Accent = FLinearColor(0.85f, 0.05f, 0.03f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor Fire = FLinearColor(1.f, 0.35f, 0.05f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor Enemy = FLinearColor(0.05f, 0.05f, 0.06f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BaseBloom = 1.2f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SunIntensityLux = 8.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FRotator SunRotation = FRotator(-38.f, 35.f, 0.f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor SunColor = FLinearColor(1.f, 0.82f, 0.62f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float Saturation = 0.75f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float Contrast = 1.25f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor ColorGain = FLinearColor(1.08f, 1.0f, 0.86f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float FilmGrain = 0.35f;
	/** Emissive of the fireball core and the ground shock ring at their peak (the bloom spike itself is BloomPeak). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float FireballGlow = 20.f;
	/** The hottest BloomPeak of any stratagem: the Flash post layer weight is a stratagem peak over this (0..1). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float MaxBloomPeak = 55.f;
	/** Sun light-shaft bloom scale (0 = off). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float LightShaftBloom = 0.15f;
	/** Strike beam glow at full charge (unlit material): well above 1 the tonemapper bleaches propaganda red to pink. */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BeamGlowPeak = 1.3f;
	/** Beam diameter (cm) when the call lands and at full charge. */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BeamWidthStartCm = 240.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BeamWidthEndCm = 30.f;
	/** The danger ring: a thin band of this many segments, this wide (cm), at this unlit glow (1 = the palette red). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") int32 DangerRingSegments = 64;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float DangerRingWidthCm = 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float DangerRingGlow = 1.f;
	/** Detonation flash of the beam: lerped toward this colour (warm orange keeps red from turning salmon), and its glow. */
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor BeamFlashColor = FLinearColor(1.f, 0.42f, 0.02f, 1.f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BeamFlashGlow = 3.f;
	/** Poster sky (cream to the zenith, not slate): the sky atmosphere's Rayleigh colour (warm, near-flat across
	 *  wavelengths), its scale and height (a taller layer keeps the zenith bright), and a warm Mie haze. */
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor SkyRayleighColor = FLinearColor(1.f, 0.86f, 0.62f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyRayleighScale = 0.0331f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyRayleighHeightKm = 24.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyMieScale = 0.012f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyMieHeightKm = 3.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor SkyMieAlbedo = FLinearColor(1.f, 0.92f, 0.78f);
	/** Sky luminance factor; divided by the weather's sun scale (floored at SkyMinSunScale) so overcast never greys it. */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyLuminance = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float SkyMinSunScale = 0.4f;
	/** Backdrop skyline past the sector edge: towers in a ring starting GapCm out, DepthCm deep; footprint and height
	 *  ranges in cm; the colour is an ink tone (the height fog hazes the far ones toward the sky). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") int32 BackdropTowers = 90;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BackdropGapCm = 2500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BackdropDepthCm = 9000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FVector2D BackdropWidthCm = FVector2D(1200.f, 3200.f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FVector2D BackdropHeightCm = FVector2D(2500.f, 9000.f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor BackdropColor = FLinearColor(0.035f, 0.032f, 0.03f);
	/** Ink outline (its strength is the post stack's Outline): line width in pixels, the relative depth step and the
	 *  normal change that count as an edge, and the distance (cm) where lines fade out (far towers stay soft). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float OutlineThicknessPx = 1.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float OutlineDepthThreshold = 0.03f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float OutlineNormalThreshold = 0.3f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float OutlineFadeCm = 25000.f;
	/** Cubes in the voxel blast core (R4). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") int32 FireballCubes = 34;
	/** Peak intensity of the blast's point light (it lights the street and the debris). */
	UPROPERTY(EditAnywhere, Config, Category = "Look") float BlastLightIntensity = 1.5e7f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float ShockGlow = 18.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float HitstopSeconds = 0.12f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float HitstopDilation = 0.05f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float FlashDecaySeconds = 0.9f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float ExposureBias = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") float FogDensity = 0.012f;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FLinearColor FogColor = FLinearColor(0.75f, 0.55f, 0.38f);
	UPROPERTY(EditAnywhere, Config, Category = "Look") float GroundSizeCm = 40000.f;
};

/** One Enhanced Input binding row. Actions are created at run time from these rows. */
USTRUCT(BlueprintType)
struct FDCKeyBinding
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Input") FName Action;
	UPROPERTY(EditAnywhere, Config, Category = "Input") FKey Key;
	UPROPERTY(EditAnywhere, Config, Category = "Input") bool bNegate = false;
	/** Swap X/Y so a key drives the Y axis of a 2D action (forward/back). */
	UPROPERTY(EditAnywhere, Config, Category = "Input") bool bSwizzle = false;
};

USTRUCT(BlueprintType)
struct FDCActionDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Input") FName Action;
	/** 0 = bool, 1 = axis1D, 2 = axis2D. */
	UPROPERTY(EditAnywhere, Config, Category = "Input") int32 Axes = 0;
};

/** An enemy kind. Behaviour is resolved by id in the enemy behaviour registry (Swarmer, Spotlight, Walker). */
USTRUCT(BlueprintType)
struct FDCEnemyDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FName Behaviour;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float MaxHealth = 40.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float Speed = 700.f;
	/** Multiplier on Speed while rushing a photographer. */
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float RushMultiplier = 1.7f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float HoverHeight = 250.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float Damage = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float AttackRange = 180.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float AttackInterval = 0.6f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float SenseRange = 3500.f;
	/** Swarm hunt (R6): beyond SenseRange the enemy drifts toward the nearest player within this range at HuntSpeedScale (0 = wander). */
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float HuntRangeCm = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float HuntSpeedScale = 0.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float Scale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FLinearColor Color = FLinearColor(0.05f, 0.05f, 0.06f);
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FLinearColor EyeColor = FLinearColor(1.f, 0.1f, 0.05f);
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") int32 KillPoints = 5;
	/** Spotlight: cone range (cm) and half-angle (deg) that blows out a photographer's exposure. */
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float LightRange = 4500.f;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float LightHalfAngle = 14.f;
	/** voxel-forge model (Content/DC/Voxel/<Model>.dcvox) worn instead of the basic shapes (R10, R17); None keeps the shapes. */
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FName Model;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") float ModelScale = 1.f;
	/** Model pivot (its bottom centre) relative to the actor, which flies at HoverHeight. */
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") FVector ModelOffset = FVector::ZeroVector;
};

/** One wave inside a sector: Count enemies of EnemyId, one every Interval seconds from StartSeconds. */
USTRUCT(BlueprintType)
struct FDCWaveDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Sector") float StartSeconds = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") FName EnemyId;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") int32 Count = 1;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") float Interval = 2.f;
	/** Special units (spotlights, walkers) spawn even when the swarm has filled the sector's MaxAlive cap. */
	UPROPERTY(EditAnywhere, Config, Category = "Sector") bool bIgnoresCap = false;
};

USTRUCT(BlueprintType)
struct FDCSectorDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Sector") FText Name;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") float DurationSeconds = 240.f;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") TArray<FDCWaveDef> Waves;
	/** Enemy cap alive at once (host spawner waits when reached). */
	UPROPERTY(EditAnywhere, Config, Category = "Sector") int32 MaxAlive = 24;
	/** HQ propaganda quota (R6): photo score the crew must file this sector, or HQ withholds the draft. */
	UPROPERTY(EditAnywhere, Config, Category = "Sector") int32 QuotaPoints = 1500;
	/** Swarm packs follow the crew's quota lag (Rules/DCPressureRules); Waves keep the special units. */
	UPROPERTY(EditAnywhere, Config, Category = "Sector") FDCPressureTuning Pressure;
};

/** Soft references to the few binary assets the game uses; each has an engine fallback. */
USTRUCT(BlueprintType)
struct FDCAssetTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath FlatMaterial = FSoftObjectPath(TEXT("/Game/DC/Materials/M_DCFlat.M_DCFlat"));
	/** Post material: ink edges from depth and normal discontinuities (R19 Outline layer field drives its strength). */
	/** Print type by role (R7, OFL faces from Content/DC/Fonts/Source, imported by tools/content/build_content.py). */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") TMap<FName, FSoftObjectPath> FontFaces = {
		{TEXT("Masthead"), FSoftObjectPath(TEXT("/Game/DC/Fonts/FF_UnifrakturMaguntia.FF_UnifrakturMaguntia"))},
		{TEXT("Headline"), FSoftObjectPath(TEXT("/Game/DC/Fonts/FF_Anton.FF_Anton"))},
		{TEXT("Body"), FSoftObjectPath(TEXT("/Game/DC/Fonts/FF_Oswald.FF_Oswald"))},
		{TEXT("Stamp"), FSoftObjectPath(TEXT("/Game/DC/Fonts/FF_BlackOpsOne.FF_BlackOpsOne"))}};
	/** Point size the HUD's scale 1.0 means; 0 = the engine large font's size (every layout was tuned on it). */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") int32 FontBaseSize = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath FlatUnlitMaterial = FSoftObjectPath(TEXT("/Game/DC/Materials/M_DCFlatUnlit.M_DCFlatUnlit"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath InkOutlineMaterial = FSoftObjectPath(TEXT("/Game/DC/Materials/M_DCInkOutline.M_DCInkOutline"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath FallbackMaterial = FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath Cube = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath Sphere = FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath Cylinder = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath Cone = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath PlayerMesh = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftClassPath PlayerAnimClass = FSoftClassPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath ReelRunAnim = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_Fwd.MF_Rifle_Jog_Fwd"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath ReelIdleAnim = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath DownedAnim = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01"));
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath DrawAnim = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Equip.MM_Rifle_Equip"));
	/** Per-slot draw motion (R1): the camera has its own two-handed raise, distinct from the rifle equip. A slot not listed uses DrawAnim. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") TMap<EDCSlot, FSoftObjectPath> SlotDrawAnims = {
		{EDCSlot::Rifle, FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Equip.MM_Rifle_Equip"))},
		{EDCSlot::Camera, FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"))},
		{EDCSlot::Stratagem, FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"))},
	};
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName DrawSlot = TEXT("DefaultSlot");
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName HandSocket = TEXT("hand_r");
	/** The press camera worn on the chest whenever it is not in hand (R10: every player reads as a photographer). */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName ChestCamModel = TEXT("presscam");
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName ChestCamSocket = TEXT("spine_05");
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FVector ChestCamLocation = FVector(0.f, 18.f, 0.f);
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FRotator ChestCamRotation = FRotator(0.f, 90.f, -90.f);
	UPROPERTY(EditAnywhere, Config, Category = "Assets") float ChestCamScale = 1.f;
	/** Holster (R1): with another slot out, the rifle rides on the back at this bone and offset instead of vanishing. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName HolsterSocket = TEXT("spine_03");
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FVector HolsterLocation = FVector(18.f, -22.f, 0.f);
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FRotator HolsterRotation = FRotator(0.f, 25.f, 90.f);
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FName HeadSocket = TEXT("head");
	/** Title menu key art (generated via media.sh, R11; ASSETS.md). */
	UPROPERTY(EditAnywhere, Config, Category = "Assets") FSoftObjectPath MenuKeyArt = FSoftObjectPath(TEXT("/Game/DC/Art/T_MenuKeyArt.T_MenuKeyArt"));
};

/** A draft option (R10). Pool groups it; Effect is resolved by id in the upgrade registry (DCUpgrades.cpp). */
USTRUCT(BlueprintType)
struct FDCUpgradeDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Id;
	/** Stratagem, Gear or Mutator. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Pool;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FText DisplayName;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FText Description;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Effect;
	/** Stratagem id an UnlockStratagem effect grants. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Unlock;
	/** Card art on the draft (R11, generated; ASSETS.md); none = a text-only card. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") FSoftObjectPath Icon;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Param = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Param2 = 0.f;

	// What an owned upgrade (gear) or an active mutator does, as data. Zero / one means "no effect".
	/** Film per sector this gear sets (0 = unchanged), and the multiplier it puts on every photo (gear). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 FilmShots = 0;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float PhotoMultiplier = 1.f;
	/** Danger bonus multiplier and extra viewfinder field of view in degrees (gear). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float DangerMultiplier = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ExtraFOV = 0.f;
	/** Photos with a blast in frame score this much more (mutators). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float BlastPhotoMultiplier = 1.f;
	/** Sun and sky light scale while active (mutators; 1 = unchanged). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float LightScale = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") bool bOverrideFog = false;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FLinearColor FogColor = FLinearColor::Black;
	/** The shutter stuns enemies in frame within this range for this long (gear). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float StunRangeCm = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float StunSeconds = 0.f;
	/** A collapse drags towers within this reach down after this delay (mutators). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float DominoReachCm = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float DominoDelaySeconds = 0.f;
	/** HQ fires a free strike on a random standing tower this often (mutators). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float RogueStrikeSeconds = 0.f;
};

/** An HQ Scoop template (R8): a photo condition with a time limit. */
USTRUCT(BlueprintType)
struct FDCScoopDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FText Text;
	/** Key into FDCPhotoInputs::Metrics; None = no metric condition. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Metric;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Threshold = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") bool bAtMost = false;
	/** Timing label the photo must earn; None = any. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") FName Timing;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float Seconds = 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 RewardPoints = 300;
};

USTRUCT(BlueprintType)
struct FDCRunTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Run") float DraftSeconds = 25.f;
	/** Solo (R12): how long a downed player has to finish the self-revive hold before the run fails. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float SelfReviveWindowSeconds = 12.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 ReelClipsPerPlayer = 2;
	/** Seconds of recorded pose before the shutter (the clip length in game time). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ClipSeconds = 2.f;
	/** Second take (R16): the camera dollies (WASD) this fast, at most this far from the original shot's camera. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float SecondTakeDollyCmPerSec = 700.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float SecondTakeRadiusCm = 2800.f;
	/** Co-op cap (R12): a listen server refuses a join beyond this many players. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 MaxPlayers = 4;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float PoseSampleHz = 12.f;
	/** Playback speed of the clip (0.4 = slow motion). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ReelSlowMo = 0.4f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ReelFreezeSeconds = 0.7f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ReelPhotoSeconds = 2.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float FinalCutSeconds = 45.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 FinalCutPicks = 3;
	/** How many of their best shots each player brings to the final cut. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 FinalCutChoices = 8;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float VoteSeconds = 30.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 VoteBonus = 600;
	/** Photo album (R24): entries kept on disk (the oldest drop first), and how many front-page picks each run adds. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 AlbumMaxEntries = 60;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 AlbumPicksPerRun = 3;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ScoopFirstSeconds = 35.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float ScoopGapSeconds = 20.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 PodFilm = 4;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 PodAmmo = 90;
	UPROPERTY(EditAnywhere, Config, Category = "Run") float PodHealth = 50.f;
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 Port = 7777;
	/** UDP port of the LAN host announcements (R12). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") int32 LanBeaconPort = 7778;
	/** War-correspondent call signs given to players in join order (machine names never show). */
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FString> CallSigns;
	/** Headline templates: {SUBJECT} {SECTOR} {PLAYER} {TIMING} are filled from the headline photo. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FString> Headlines;
	/** Category tag -> headline subject words. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") TMap<FName, FString> Subjects;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Director's Cut Tuning"))
class EPIC_AI_MEGA_JAM2_API UDCTuning : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDCTuning& Get() { return *GetDefault<UDCTuning>(); }

	UPROPERTY(EditAnywhere, Config, Category = "Assets") FDCAssetTuning Assets;
	UPROPERTY(EditAnywhere, Config, Category = "Photo") FDCPhotoTuning Photo;
	UPROPERTY(EditAnywhere, Config, Category = "Move") FDCMoveTuning Move;
	UPROPERTY(EditAnywhere, Config, Category = "Rifle") FDCRifleTuning Rifle;
	UPROPERTY(EditAnywhere, Config, Category = "Camera") FDCCameraTuning Camera;
	UPROPERTY(EditAnywhere, Config, Category = "Economy") FDCEconomyTuning Economy;
	UPROPERTY(EditAnywhere, Config, Category = "City") FDCCityTuning City;
	UPROPERTY(EditAnywhere, Config, Category = "Look") FDCLookTuning Look;
	UPROPERTY(EditAnywhere, Config, Category = "Strike") TArray<FDCStratagemDef> Stratagems;
	UPROPERTY(EditAnywhere, Config, Category = "Enemy") TArray<FDCEnemyDef> Enemies;
	UPROPERTY(EditAnywhere, Config, Category = "Sector") TArray<FDCSectorDef> Sectors;
	UPROPERTY(EditAnywhere, Config, Category = "Input") TArray<FDCActionDef> Actions;
	UPROPERTY(EditAnywhere, Config, Category = "Input") TArray<FDCKeyBinding> Bindings;
	UPROPERTY(EditAnywhere, Config, Category = "Run") FDCRunTuning Run;
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FDCUpgradeDef> Upgrades;
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FDCScoopDef> Scoops;
	/** Full edition front page (R22): satirical headline rows per photo category. */
	UPROPERTY(EditAnywhere, Config, Category = "Run") TArray<FDCHeadlineRow> HeadlineRows;
	/** Post-process layer stack (R19): every layer of the scene and the photo look; code only sets weights by id. */
	UPROPERTY(EditAnywhere, Config, Category = "Post") TArray<FDCPostLayer> PostLayers;
	UPROPERTY(EditAnywhere, Config, Category = "Post") TArray<FDCPostPreset> PostPresets;
	/** Seasonal weather (R18): profiles the arcs blend, and seasons with their arcs and snow cover. */
	UPROPERTY(EditAnywhere, Config, Category = "Weather") TArray<FDCWeatherProfile> WeatherProfiles;
	UPROPERTY(EditAnywhere, Config, Category = "Weather") TArray<FDCSeasonDef> Seasons;
	/** Preset applied at start (-dcpost=<Id> on the command line wins). */
	UPROPERTY(EditAnywhere, Config, Category = "Post") FName PostPreset = TEXT("High");

	const FDCStratagemDef* FindStratagem(FName Id) const
	{
		return Stratagems.FindByPredicate([Id](const FDCStratagemDef& D) { return D.Id == Id; });
	}
	const FDCUpgradeDef* FindUpgrade(FName Id) const
	{
		return Upgrades.FindByPredicate([Id](const FDCUpgradeDef& D) { return D.Id == Id; });
	}
	const FDCEnemyDef* FindEnemy(FName Id) const
	{
		return Enemies.FindByPredicate([Id](const FDCEnemyDef& D) { return D.Id == Id; });
	}
};
