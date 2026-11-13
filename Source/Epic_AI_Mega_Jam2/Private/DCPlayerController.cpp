#include "DCPlayerController.h"

#include "DCAudio.h"
#include "DCInput.h"
#include "DCTuning.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

ADCPlayerController::ADCPlayerController()
{
	bShowMouseCursor = false;
}

UDCInputSet* ADCPlayerController::GetInputSet()
{
	if (!InputSet)
	{
		InputSet = NewObject<UDCInputSet>(this);
		InputSet->Build();
	}
	return InputSet;
}

void ADCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	Sensitivity = UDCTuning::Get().Move.MouseSensitivity;
	if (!IsLocalController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->AddMappingContext(GetInputSet()->GetContext(), 0);
	}
	SetInputMode(FInputModeGameOnly());
	if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
	{
		Audio->StartMusic(this, TEXT("Music"), 0.35f);
	}
}

void ADCPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ADCPlayerController::OnSensUp);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ADCPlayerController::OnSensDown);
}

void ADCPlayerController::SetSensitivity(float Value)
{
	Sensitivity = FMath::Clamp(Value, 0.05f, 5.f);
}

void ADCPlayerController::OnSensUp() { SetSensitivity(Sensitivity * 1.15f); DCAudio::Play2D(this, TEXT("UIClick")); }
void ADCPlayerController::OnSensDown() { SetSensitivity(Sensitivity / 1.15f); DCAudio::Play2D(this, TEXT("UIClick")); }

void ADCPlayerController::AddLocalPhoto(int32 PhotoId, UTextureRenderTarget2D* Image, const FDCPhotoGrade& Preview, const FDCShotView& View, float ShotTime)
{
	FDCLocalPhoto& Photo = Album.AddDefaulted_GetRef();
	Photo.PhotoId = PhotoId;
	Photo.Image = Image;
	Photo.Grade = Preview;
	Photo.View = View;
	Photo.ShotTime = ShotTime;
	Photo.TakenReal = GetWorld()->GetRealTimeSeconds();
}

void ADCPlayerController::ConfirmLocalPhoto(int32 PhotoId, const FDCPhotoGrade& Grade)
{
	FDCLocalPhoto* Photo = Album.FindByPredicate([PhotoId](const FDCLocalPhoto& P) { return P.PhotoId == PhotoId; });
	if (!Photo)
	{
		return;
	}
	Photo->Grade = Grade;
	Photo->bConfirmed = true;
	DCAudio::Play2D(this, TEXT("Stamp"), 0.9f);
}

const FDCLocalPhoto* ADCPlayerController::FindLocalPhoto(int32 PhotoId) const
{
	return Album.FindByPredicate([PhotoId](const FDCLocalPhoto& P) { return P.PhotoId == PhotoId; });
}
