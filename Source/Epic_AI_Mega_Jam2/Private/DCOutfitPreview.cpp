#include "DCOutfitPreview.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCOutfitBuild.h"
#include "DCPalette.h"
#include "DCTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

ADCOutfitPreview::ADCOutfitPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Turn = CreateDefaultSubobject<USceneComponent>(TEXT("Turn"));
	Turn->SetupAttachment(RootComponent);
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Turn);
	Body->SetRelativeLocationAndRotation(FVector(0, 0, -90.f), FRotator(0, -90.f, 0)); // the character's mesh offset
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(RootComponent);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(RootComponent);
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	Capture->FOVAngle = 30.f;
}

float ADCOutfitPreview::ImageAlpha() const
{
	const UWorld* W = GetWorld();
	return W ? FMath::Clamp(static_cast<float>(W->GetRealTimeSeconds() - StartReal - 0.6) / 0.3f, 0.f, 1.f) : 0.f;
}

void ADCOutfitPreview::BeginPlay()
{
	Super::BeginPlay();
	StartReal = GetWorld()->GetRealTimeSeconds();
	const UDCTuning& T = UDCTuning::Get();
	if (USkeletalMesh* Mannequin = Cast<USkeletalMesh>(T.Assets.PlayerMesh.TryLoad()))
	{
		Body->SetSkeletalMesh(Mannequin);
		if (UClass* Anim = T.Assets.PlayerAnimClass.TryLoadClass<UAnimInstance>())
		{
			Body->SetAnimInstanceClass(Anim); // idle breathing, so the parts read as worn
		}
	}
	Backdrop->SetStaticMesh(DCAssets::Cube());
	Backdrop->SetMaterial(0, DCAssets::MakeFlatUnlit(this, DCPalette::Paper, 1.f));
	Backdrop->SetRelativeLocation(FVector(-150.f, 0, 0));
	Backdrop->SetRelativeScale3D(FVector(0.1f, 6.f, 6.f));
	Image = NewObject<UTextureRenderTarget2D>(this);
	Image->InitAutoFormat(512, 640);
	Capture->TextureTarget = Image;
	// Full length, slightly from above, the way the street camera sees a teammate.
	Capture->SetRelativeLocationAndRotation(FVector(420.f, 0, 30.f), FRotator(-6.f, 180.f, 0));
	Capture->ShowOnlyActors.Add(this);
	// Key and rim lights of its own (the noir world light leaves a dark coat black in the menu).
	auto AddLight = [this](const FVector& At, const FLinearColor& Color, float Candela)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(RootComponent);
		L->SetRelativeLocation(At);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(Candela);
		L->SetLightColor(Color);
		L->SetAttenuationRadius(900.f);
		L->SetCastShadows(false);
		L->RegisterComponent();
	};
	const FDCLookTuning& Look = T.Look;
	AddLight(FVector(320.f, 140.f, 120.f), Look.OutfitKeyColor, Look.OutfitKeyCandela);
	AddLight(FVector(-120.f, -160.f, 160.f), Look.OutfitRimColor, Look.OutfitRimCandela);
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = Look.OutfitExposureBias;
}

void ADCOutfitPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bHold)
	{
		Turn->AddRelativeRotation(FRotator(0, 35.f * DeltaSeconds, 0));
	}
}

void ADCOutfitPreview::Face(float Yaw)
{
	bHold = Yaw >= 0.f;
	if (bHold)
	{
		Turn->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	}
}

bool ADCOutfitPreview::SaveImage(const FString& Path)
{
	if (!Capture || !Image)
	{
		return false;
	}
	Capture->CaptureScene();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*Path));
	return Ar && FImageUtils::ExportRenderTarget2DAsPNG(Image, *Ar);
}

void ADCOutfitPreview::Show(const FDCOutfit& Outfit)
{
	DCOutfitBuild::Apply(Body, Outfit, Parts);
}
