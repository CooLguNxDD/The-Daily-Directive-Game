#include "DCPlayerController.h"

#include "DCAudio.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCInput.h"
#include "DCLanBeacon.h"
#include "DCMenu.h"
#include "DCPlayerState.h"
#include "Rules/DCPhotoRules.h"
#include "Rules/DCRunRules.h"
#include "DCReelDirector.h"
#include "DCDemoPilot.h"
#include "DCTuning.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "IImageWrapper.h"
#include "Rules/DCNewsprint.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UnrealClient.h"

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
	if (ADCDemoPilot::IsMedicRequested() && !ADCDemoPilot::IsRequested())
	{
		GetWorld()->SpawnActor<ADCDemoPilot>();
	}
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
	// Leaving the title menu (solo, join, quit): stop listening for LAN games. A host's announcement is the game mode's.
	UDCLanBeacon* Beacon = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDCLanBeacon>() : nullptr;
	if (Beacon && Beacon->IsListening())
	{
		Beacon->Stop();
	}
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

void ADCPlayerController::Confirm() { DCAudio::Play2D(this, TEXT("UIConfirm"), 0.9f); }
void ADCPlayerController::OnSensUp() { SetSensitivity(Sensitivity * 1.15f); DCAudio::Play2D(this, TEXT("UIClick")); }
void ADCPlayerController::OnSensDown() { SetSensitivity(Sensitivity / 1.15f); DCAudio::Play2D(this, TEXT("UIClick")); }

void ADCPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Runs on every copy: the client uploads, and the host-side copy of a remote player relays down to it.
	PumpSends();
}

void ADCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (IsLocalController())
	{
		UpdateMenu();
		UpdateCursor();
		UpdateReel();
		UpdateEvidenceShots();
		UpdateAutoVote();
	}
}

void ADCPlayerController::UpdateAutoVote()
{
	// Scripted co-op evidence (-dcautovote): this player votes for the first shot that is not theirs.
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const ADCPlayerState* Mine = GetPlayerState<ADCPlayerState>();
	if (!FParse::Param(FCommandLine::Get(), TEXT("dcautovote")) || !GS || !Mine || GS->Phase != EDCRunPhase::Vote || Mine->VotedFor != 0 || bAutoVoted)
	{
		return;
	}
	// Vote the runner-up among the other players' picks: within 600 of the leader, the vote flips the headline.
	TArray<TPair<int32, const ADCPlayerState*>> Picks;
	for (const APlayerState* P : GS->PlayerArray)
	{
		const ADCPlayerState* Other = Cast<ADCPlayerState>(P);
		for (int32 Id : (Other && Other != Mine) ? Other->FinalCut : TArray<int32>())
		{
			Picks.Add(TPair<int32, const ADCPlayerState*>(Id, Other));
		}
	}
	if (Picks.Num() == 0)
	{
		return;
	}
	Picks.Sort([](const TPair<int32, const ADCPlayerState*>& A, const TPair<int32, const ADCPlayerState*>& B)
	{
		const FDCPhotoRecord* PA = A.Value->FindPhoto(A.Key);
		const FDCPhotoRecord* PB = B.Value->FindPhoto(B.Key);
		return (PA ? PA->Grade.Score : 0) > (PB ? PB->Grade.Score : 0);
	});
	const TPair<int32, const ADCPlayerState*>& Choice = Picks[FMath::Min(1, Picks.Num() - 1)];
	bAutoVoted = true;
	UE_LOG(LogTemp, Display, TEXT("DCVOTE %s votes for photo %d by %s"), *Mine->GetPlayerName(), Choice.Key, *Choice.Value->GetPlayerName());
	RequestVote(Choice.Key);
}

void ADCPlayerController::UpdateEvidenceShots()
{
	float Every = 0.f;
	FString Dir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("dcshotevery="), Every) || Every <= 0.f || !FParse::Value(FCommandLine::Get(), TEXT("dcshots="), Dir))
	{
		return;
	}
	const double Real = GetWorld()->GetRealTimeSeconds();
	if (Real < NextEvidenceShotReal)
	{
		return;
	}
	NextEvidenceShotReal = Real + Every;
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	// The real-time stamp keeps frames apart across a travel (a new controller restarts the index).
	const FString File = Dir / FString::Printf(TEXT("%03d_t%04d_phase%d.png"), EvidenceShotIndex++, static_cast<int32>(FPlatformTime::Seconds()) % 10000, GS ? static_cast<int32>(GS->Phase) : -1);
	FScreenshotRequest::RequestScreenshot(File, true, false);
	UE_LOG(LogTemp, Display, TEXT("DCSHOT %s net=%d players=%d collapses=%d blasts=%d"), *File, static_cast<int32>(GetNetMode()), GS ? GS->PlayerArray.Num() : 0,
		GS ? GS->Collapses.Num() : 0, GS ? GS->Blasts.Num() : 0);
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
	// The title menu listens for LAN hosts and rebuilds when the list changes.
	UDCLanBeacon* Beacon = GetGameInstance()->GetSubsystem<UDCLanBeacon>();
	TArray<FDCLanHost> Hosts = Mode == 0 && Beacon ? Beacon->GetHosts() : TArray<FDCLanHost>();
	FString HostsKey;
	for (const FDCLanHost& H : Hosts)
	{
		HostsKey += H.Address + TEXT("/") + FString::FromInt(H.Players) + TEXT(";");
	}
	if (Beacon && Mode == 0 && !Beacon->IsListening())
	{
		Beacon->StartListening();
	}
	// Evidence runs (-dcmenushot=<file>) frame the title menu after 4 real seconds, then quit.
	static FString MenuShot;
	static const bool bMenuShot = FParse::Value(FCommandLine::Get(), TEXT("dcmenushot="), MenuShot);
	if (bMenuShot && Mode == 0)
	{
		const double Real = GetWorld()->GetRealTimeSeconds();
		static bool bShotTaken = false;
		if (!bShotTaken && Real > 4.0)
		{
			bShotTaken = true;
			FScreenshotRequest::RequestScreenshot(MenuShot, true, false);
		}
		if (Real > 6.0)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
	}
	// Evidence runs (-dclanjoin=S) press the first LAN game's JOIN after S real seconds, the same path as the button.
	float LanJoinAfter = 0.f;
	if (Mode == 0 && MenuMode == 0 && Hosts.Num() > 0 && FParse::Value(FCommandLine::Get(), TEXT("dclanjoin="), LanJoinAfter)
		&& GetWorld()->GetRealTimeSeconds() >= LanJoinAfter && !bLanJoinSent)
	{
		bLanJoinSent = true;
		UE_LOG(LogTemp, Display, TEXT("DCLAN joining %s heard on the LAN"), *Hosts[0].Address);
		MenuJoin(Hosts[0].Address);
	}
	if ((Mode == MenuMode && HostsKey == MenuHostsKey) || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (Beacon && Mode != 0 && MenuMode == 0)
	{
		Beacon->Stop();
	}
	MenuHostsKey = HostsKey;
	if (MenuWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		MenuWidget.Reset();
	}
	MenuMode = Mode;
	if (Mode != 255)
	{
		MenuWidget = DCMenu::Build(this, Mode, Hosts);
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

void ADCPlayerController::ServerSecondTake_Implementation(int32 PhotoId, FVector_NetQuantize10 CamLoc, FRotator CamRot, float FOV, int32 ClipIndex)
{
	ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const FDCCameraTuning& C = UDCTuning::Get().Camera;
	// One second take per clip per player, only while that clip plays, from near the replayed scene; graded by
	// the host on its own re-staged scene at the host's replay clock.
	if (!GS || !PS || GS->Phase != EDCRunPhase::Reel || GS->ReelIndex != ClipIndex || GS->ReelStage > 1 || SecondTakeClips.Contains(ClipIndex) || PS->FindPhoto(PhotoId)
		|| !GS->ReelClips.IsValidIndex(ClipIndex) || FVector::Dist(CamLoc, GS->ReelClips[ClipIndex].Photo.CameraLocation) > UDCTuning::Get().Run.SecondTakeRadiusCm + 100.f)
	{
		if (!PS || !PS->FindPhoto(PhotoId))
		{
			ClientSecondTakeRejected(PhotoId, ClipIndex); // the client may try this clip again while it still plays
		}
		return;
	}
	SecondTakeClips.Add(ClipIndex);
	const FDCReelClip& Clip = GS->ReelClips[ClipIndex];
	const float WindowStart = Clip.Photo.ShotTime - UDCTuning::Get().Run.ClipSeconds;
	const float When = FMath::Clamp(WindowStart + (GS->Now() - GS->ReelClipStart), WindowStart, Clip.Photo.ShotTime);
	FDCShotView View;
	View.Location = CamLoc;
	View.Rotation = CamRot;
	View.FOV = FMath::Clamp(FOV, C.ViewfinderFOVTight, C.ViewfinderFOVWide + PS->ExtraFOV);
	FDCPhotoRecord Record;
	Record.PhotoId = PhotoId;
	Record.OwnerPlayerId = PS->GetPlayerId();
	Record.Sector = Clip.Photo.Sector;
	Record.ShotTime = When;
	Record.CameraLocation = CamLoc;
	Record.CameraRotation = CamRot;
	Record.FOV = View.FOV;
	Record.bSecondTake = true;
	Record.Grade = DCPhotoRules::GradePhoto(DCPhotoProbe::Measure(GetWorld(), View, When, nullptr, false, Record.BlastId), UDCTuning::Get().Photo);
	PS->AddPhoto(Record);
	DCRunRules::InsertByCritic(PS->FinalCutChoices, PS->Photos, PhotoId); // second takes enter the ranking at their critic rank
	UE_LOG(LogTemp, Display, TEXT("DCTAKE %s second take photo=%d clip=%d score=%d timing=%s camera %.0f cm from the original shot, fov %.0f"), *PS->GetPlayerName(), PhotoId, ClipIndex,
		Record.Grade.Score, *Record.Grade.TimingLabel.ToString(), FVector::Dist(CamLoc, Clip.Photo.CameraLocation), View.FOV);
	ClientSecondTakeGraded(PhotoId, Record.Grade);
}

void ADCPlayerController::ClientSecondTakeGraded_Implementation(int32 PhotoId, FDCPhotoGrade Grade)
{
	ConfirmLocalPhoto(PhotoId, Grade);
	ClientUploadPhotos_Implementation({PhotoId}); // every machine gets the image for the final cut and vote
}

void ADCPlayerController::ClientSecondTakeRejected_Implementation(int32 PhotoId, int32 ClipIndex)
{
	Album.RemoveAll([PhotoId](const FDCLocalPhoto& P) { return P.PhotoId == PhotoId; });
	if (Director)
	{
		Director->OnTakeRejected(ClipIndex);
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
	// Print it (R5): read the capture back once at the shutter, levels + halftone + grain + border on the CPU, and
	// show and upload the print from then on.
	FTextureRenderTargetResource* Resource = Image ? Image->GameThread_GetRenderTargetResource() : nullptr;
	if (Resource && Resource->ReadPixels(Photo.Pixels) && Photo.Pixels.Num() == Image->SizeX * Image->SizeY)
	{
		Photo.Width = Image->SizeX;
		Photo.Height = Image->SizeY;
		DCNewsprint::Print(Photo.Pixels, Photo.Width, Photo.Height, UDCTuning::Get().Camera.Print, static_cast<uint32>(PhotoId));
		UTexture2D* Print = UTexture2D::CreateTransient(Photo.Width, Photo.Height, PF_B8G8R8A8);
		if (Print)
		{
			void* Mip = Print->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Mip, Photo.Pixels.GetData(), Photo.Pixels.Num() * sizeof(FColor));
			Print->GetPlatformData()->Mips[0].BulkData.Unlock();
			Print->SRGB = true;
			Print->UpdateResource();
			Photo.Image = Print;
		}
	}
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
		if (!Photo || Photo->Pixels.Num() == 0)
		{
			continue;
		}
		TArray<FColor> Pixels = Photo->Pixels;
		for (FColor& C : Pixels)
		{
			C.A = 255;
		}
		TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
		const int32 W = Photo->Width, H = Photo->Height;
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
