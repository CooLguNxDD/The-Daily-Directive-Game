#include "DCOutfitPreview.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCOutfitBuild.h"
#include "DCPalette.h"
#include "DCTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
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

void ADCOutfitPreview::BeginPlay()
{
	Super::BeginPlay();
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
}

void ADCOutfitPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Turn->AddRelativeRotation(FRotator(0, 35.f * DeltaSeconds, 0));
}

void ADCOutfitPreview::Show(const FDCOutfit& Outfit)
{
	DCOutfitBuild::Apply(Body, Outfit, Parts);
}
