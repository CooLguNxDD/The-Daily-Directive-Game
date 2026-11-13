#include "DCPlayerController.h"

#include "DCAudio.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCInput.h"
#include "DCMenu.h"
#include "DCPlayerState.h"
#include "DCReelDirector.h"
#include "DCTuning.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 ChunkBytes = 16000;
	constexpr int32 ChunksPerTick = 3;
	constexpr int32 JpegQuality = 80;

	/** Which menu the lobby shows: 0 main menu (not connected), 1 hosting, 2 joined. */
	uint8 MenuModeFor(const APlayerController& PC)
	{
		const ENetMode Mode = PC.GetNetMode();
		return Mode == NM_Standalone ? 0 : (Mode == NM_ListenServer ? 1 : 2);
	}

	/** Phases in which the player clicks the HUD instead of aiming. */
	bool WantsCursor(EDCRunPhase Phase)
	{
		static const TSet<EDCRunPhase> CursorPhases = {EDCRunPhase::Lobby, EDCRunPhase::Draft, EDCRunPhase::FinalCut, EDCRunPhase::Vote, EDCRunPhase::FrontPage};
		return CursorPhases.Contains(Phase);
	}
}

ADCPlayerController::ADCPlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = true;
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

void ADCPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	Super::EndPlay(Reason);
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

void ADCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	PumpSends();
	if (IsLocalController())
	{
		UpdateMenu();
		UpdateCursor();
		UpdateReel();
	}
}

void ADCPlayerController::UpdateReel()
{
	// Every machine runs its own reel director while the host shows the reel.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const bool bWant = GS && GS->Phase == EDCRunPhase::Reel;
	if (bWant && !Director)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Director = GetWorld()->SpawnActor<ADCReelDirector>(ADCReelDirector::StaticClass(), FTransform::Identity, Params);
		return;
	}
	if (!bWant && Director)
	{
		Director->Destroy();
		Director = nullptr;
	}
}

// ---------------------------------------------------------------- menu and cursor

void ADCPlayerController::UpdateMenu()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const bool bWant = GS && GS->Phase == EDCRunPhase::Lobby && !FParse::Param(FCommandLine::Get(), TEXT("dcdemo"));
	const uint8 Mode = bWant ? MenuModeFor(*this) : 255;
	if (Mode == MenuMode || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (MenuWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		MenuWidget.Reset();
	}
	MenuMode = Mode;
	if (Mode != 255)
	{
		MenuWidget = DCMenu::Build(this, Mode);
		GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 10);
	}
}

void ADCPlayerController::UpdateCursor()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const bool bCursor = GS && WantsCursor(GS->Phase) && !FParse::Param(FCommandLine::Get(), TEXT("dcdemo"));
	if (bCursor == bShowMouseCursor)
	{
		return;
	}
	bShowMouseCursor = bCursor;
	if (bCursor)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		return;
	}
	SetInputMode(FInputModeGameOnly());
}

void ADCPlayerController::MenuSolo()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, TEXT("solo"));
}

void ADCPlayerController::MenuHost()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, TEXT("listen"));
}

void ADCPlayerController::MenuJoin(const FString& Address)
{
	FString Target = Address.TrimStartAndEnd();
	if (Target.IsEmpty())
	{
		return;
	}
	if (!Target.Contains(TEXT(":")))
	{
		Target += FString::Printf(TEXT(":%d"), UDCTuning::Get().Run.Port);
	}
	ClientTravel(Target, TRAVEL_Absolute);
}

void ADCPlayerController::MenuQuit()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

// ---------------------------------------------------------------- run intent (validated by the game mode)

void ADCPlayerController::ServerStartRun_Implementation()
{
	// Only the host's own controller may start the run.
	ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
	if (GM && IsLocalController())
	{
		GM->StartRun();
	}
}

void ADCPlayerController::ServerRestartRun_Implementation()
{
	ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>();
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (GM && IsLocalController() && GS && GS->Phase == EDCRunPhase::FrontPage)
	{
		GM->RestartRun();
	}
}

void ADCPlayerController::ServerPickUpgrade_Implementation(FName Id)
{
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		GM->PickUpgrade(GetPlayerState<ADCPlayerState>(), Id);
	}
}

void ADCPlayerController::ToggleFinalCutPick(int32 PhotoId)
{
	const int32 Max = UDCTuning::Get().Run.FinalCutPicks;
	if (LocalPicks.Remove(PhotoId) == 0 && LocalPicks.Num() < Max)
	{
		LocalPicks.Add(PhotoId);
	}
	DCAudio::Play2D(this, TEXT("UIClick"));
}

void ADCPlayerController::ServerSubmitFinalCut_Implementation(const TArray<int32>& PhotoIds)
{
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		GM->SubmitFinalCut(GetPlayerState<ADCPlayerState>(), PhotoIds);
	}
}

void ADCPlayerController::ServerVote_Implementation(int32 PhotoId)
{
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		GM->CastVote(GetPlayerState<ADCPlayerState>(), PhotoId);
	}
}

// ---------------------------------------------------------------- album

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

UTexture* ADCPlayerController::GetPhotoImage(int32 PhotoId) const
{
	if (const FDCLocalPhoto* Own = FindLocalPhoto(PhotoId))
	{
		return Own->Image;
	}
	const TObjectPtr<UTexture>* Found = Library.Find(PhotoId);
	return Found ? Found->Get() : nullptr;
}

// ---------------------------------------------------------------- photo transfer

void ADCPlayerController::ClientUploadPhotos_Implementation(const TArray<int32>& PhotoIds)
{
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	for (int32 Id : PhotoIds)
	{
		const FDCLocalPhoto* Photo = FindLocalPhoto(Id);
		FTextureRenderTargetResource* Resource = Photo && Photo->Image ? Photo->Image->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource)
		{
			continue;
		}
		TArray<FColor> Pixels;
		if (!Resource->ReadPixels(Pixels) || Pixels.Num() == 0)
		{
			continue;
		}
		for (FColor& C : Pixels)
		{
			C.A = 255;
		}
		TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
		const int32 W = Photo->Image->SizeX, H = Photo->Image->SizeY;
		if (!Jpeg.IsValid() || !Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		{
			continue;
		}
		TArray64<uint8> Compressed = Jpeg->GetCompressed(JpegQuality);
		TArray<uint8> Bytes(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
		if (HasAuthority())
		{
			HostReceivedPhoto(Id, Bytes); // the listen host's own photos skip the wire
			continue;
		}
		Outgoing.Add({Id, MoveTemp(Bytes), 0});
	}
}

void ADCPlayerController::PumpSends()
{
	int32 Budget = ChunksPerTick;
	while (Budget > 0 && Outgoing.Num() > 0)
	{
		FDCPhotoSend& Send = Outgoing[0];
		const int32 Count = FMath::Max(1, FMath::DivideAndRoundUp(Send.Bytes.Num(), ChunkBytes));
		const int32 Start = Send.NextChunk * ChunkBytes;
		const TArray<uint8> Chunk(Send.Bytes.GetData() + Start, FMath::Min(ChunkBytes, Send.Bytes.Num() - Start));
		// Client side: upload to the host. Host side (a remote player's controller): relay down to that player.
		if (IsLocalController())
		{
			ServerPhotoChunk(Send.PhotoId, Send.NextChunk, Count, Chunk);
		}
		else
		{
			ClientPhotoChunk(Send.PhotoId, Send.NextChunk, Count, Chunk);
		}
		--Budget;
		if (++Send.NextChunk >= Count)
		{
			Outgoing.RemoveAt(0);
		}
	}
}

void ADCPlayerController::Reassemble(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes, bool bOnHost)
{
	if (Count <= 0 || Count > 64 || Index < 0 || Index >= Count || Bytes.Num() > ChunkBytes)
	{
		return; // malformed: outside input is data
	}
	TArray<TArray<uint8>>& Parts = Incoming.FindOrAdd(PhotoId);
	if (Parts.Num() != Count)
	{
		Parts.SetNum(Count);
	}
	Parts[Index] = Bytes;
	if (Parts.ContainsByPredicate([](const TArray<uint8>& P) { return P.Num() == 0; }))
	{
		return;
	}
	TArray<uint8> Whole;
	for (const TArray<uint8>& P : Parts)
	{
		Whole.Append(P);
	}
	Incoming.Remove(PhotoId);
	if (bOnHost)
	{
		HostReceivedPhoto(PhotoId, Whole);
		return;
	}
	StoreImage(PhotoId, Whole);
}

void ADCPlayerController::ServerPhotoChunk_Implementation(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes)
{
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	if (!PS || !PS->FindPhoto(PhotoId))
	{
		return; // a player may only upload their own graded photos
	}
	Reassemble(PhotoId, Index, Count, Bytes, true);
}

void ADCPlayerController::ClientPhotoChunk_Implementation(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes)
{
	Reassemble(PhotoId, Index, Count, Bytes, false);
}

void ADCPlayerController::HostReceivedPhoto(int32 PhotoId, const TArray<uint8>& Bytes)
{
	for (ADCPlayerController* PC : TActorRange<ADCPlayerController>(GetWorld()))
	{
		if (PC == this)
		{
			continue; // the photographer already has it
		}
		if (PC->IsLocalController())
		{
			PC->StoreImage(PhotoId, Bytes); // the listen host sees it at once
			continue;
		}
		PC->Outgoing.Add({PhotoId, Bytes, 0});
	}
}

void ADCPlayerController::StoreImage(int32 PhotoId, const TArray<uint8>& Bytes)
{
	if (UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(Bytes))
	{
		Library.Add(PhotoId, Tex);
	}
}
