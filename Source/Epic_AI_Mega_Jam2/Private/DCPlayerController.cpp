#include "DCPlayerController.h"
#include "UObject/UObjectGlobals.h"
#include "DCUnits.h"
#include "DCCharacter.h"
#include "DCEnvironment.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DCOutfitBuild.h"
#include "DCOutfitPreview.h"
#include "Rules/DCOutfit.h"
#include "DCSave.h"
#include "RenderTimer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "RHI.h"
#include "DCVoxelCity.h"

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
#include "GameFramework/CharacterMovementComponent.h"
#include "Widgets/SWindow.h"
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
	if (const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		Sensitivity = FMath::Clamp(Save->Settings.MouseSensitivity, 0.05f, 5.f); // saved settings (R24)
		// Saved window mode, unless a run asked for a window on the command line (evidence and perf runs).
		if (Save->Settings.bFullscreen && IsLocalController() && GEngine && GEngine->GetGameUserSettings() && !FParse::Param(FCommandLine::Get(), TEXT("windowed")))
		{
			GEngine->GetGameUserSettings()->SetFullscreenMode(EWindowMode::WindowedFullscreen);
			GEngine->GetGameUserSettings()->ApplySettings(false);
		}
	}
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
		Audio->StartMusic(this, TEXT("Music"), UDCTuning::Get().Run.MusicVolume); // times the saved master volume
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
	if (UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		Save->Settings.MouseSensitivity = Sensitivity;
		Save->SaveSettings();
	}
}

void ADCPlayerController::SendOutfit()
{
	const UDCTuning& T = UDCTuning::Get();
	FString Code = DCOutfit::Encode(DCOutfit::Default(T.OutfitSlots));
	if (const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		if (Save->Characters.Slots.IsValidIndex(Save->Characters.ActiveSlot))
		{
			Code = Save->Characters.Slots[Save->Characters.ActiveSlot];
		}
	}
	ServerSetOutfit(Code);
}

void ADCPlayerController::ServerSetOutfit_Implementation(const FString& Code)
{
	const UDCTuning& T = UDCTuning::Get();
	FDCOutfit Outfit;
	FString Error;
	if (!DCOutfitBuild::Decode(Code, Outfit, Error))
	{
		UE_LOG(LogTemp, Display, TEXT("DCOUTFIT rejected '%s': %s (default kept)"), *Code.Left(40), *Error);
		Outfit = DCOutfit::Default(T.OutfitSlots);
	}
	if (ADCPlayerState* PS = GetPlayerState<ADCPlayerState>())
	{
		PS->SetOutfitCode(DCOutfit::Encode(Outfit));
		UE_LOG(LogTemp, Display, TEXT("DCOUTFIT %s wears %s"), *PS->GetPlayerName(), *PS->OutfitCode);
	}
}

void ADCPlayerController::SettingsStep(FName Row, int32 Step)
{
	UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	if (!Save)
	{
		return;
	}
	FDCSettingsSave& S = Save->Settings;
	using FStep = TFunction<void(ADCPlayerController&, FDCSettingsSave&, int32)>;
	static const TMap<FName, FStep> Steps = {
		{TEXT("Volume"), [](ADCPlayerController&, FDCSettingsSave& S, int32 D) { S.MasterVolume = FMath::Clamp(FMath::RoundToFloat((S.MasterVolume + D * 0.1f) * 10.f) / 10.f, 0.f, 1.f); }},
		{TEXT("Mouse"), [](ADCPlayerController& P, FDCSettingsSave&, int32 D) { P.SetSensitivity(P.GetSensitivity() * (D > 0 ? 1.15f : 1.f / 1.15f)); }},
		{TEXT("Quality"), [](ADCPlayerController&, FDCSettingsSave& S, int32 D) { S.Quality = FMath::Clamp(S.Quality + D, 0, 2); }},
		{TEXT("Window"), [](ADCPlayerController&, FDCSettingsSave& S, int32) { S.bFullscreen = !S.bFullscreen; }},
	};
	if (const FStep* Fn = Steps.Find(Row))
	{
		(*Fn)(*this, S, Step);
	}
	Save->SaveSettings();
	if (Row == TEXT("Volume"))
	{
		if (UDCAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UDCAudioSubsystem>())
		{
			Audio->ApplyMasterVolume(this); // the music follows at once
		}
	}
	if (Row == TEXT("Quality"))
	{
		if (ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>(); GS && GS->GetEnvironment())
		{
			GS->GetEnvironment()->ApplyPreset();
		}
	}
	if (Row == TEXT("Window") && GEngine && GEngine->GetGameUserSettings())
	{
		GEngine->GetGameUserSettings()->SetFullscreenMode(S.bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		GEngine->GetGameUserSettings()->ApplySettings(false);
	}
	DCAudio::Play2D(this, TEXT("UIClick"));
	MenuMode = 254;
}

FString ADCPlayerController::KeyFor(FName Action) const
{
	const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	if (const FString* Saved = Save ? Save->Settings.KeyBindings.Find(Action.ToString()) : nullptr)
	{
		return FKey(**Saved).GetDisplayName().ToString().ToUpper();
	}
	const FDCKeyBinding* Row = UDCTuning::Get().Bindings.FindByPredicate([Action](const FDCKeyBinding& B) { return B.Action == Action; });
	return Row ? Row->Key.GetDisplayName().ToString().ToUpper() : FString(TEXT("-"));
}

bool ADCPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (!AwaitingKey.IsNone() && Params.Event == IE_Pressed && !Params.Key.IsAnalog())
	{
		UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
		if (Save && Params.Key != EKeys::Escape)
		{
			Save->Settings.KeyBindings.Add(AwaitingKey.ToString(), Params.Key.GetFName().ToString());
			Save->SaveSettings();
			RebuildInput();
			UE_LOG(LogTemp, Display, TEXT("DCKEYS %s -> %s (saved)"), *AwaitingKey.ToString(), *Params.Key.GetFName().ToString());
		}
		AwaitingKey = NAME_None;
		MenuMode = 254;
		return true;
	}
	return Super::InputKey(Params);
}

void ADCPlayerController::ResetKeys()
{
	if (UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		Save->Settings.KeyBindings.Reset();
		Save->SaveSettings();
	}
	AwaitingKey = NAME_None;
	RebuildInput();
	MenuMode = 254;
}

void ADCPlayerController::RebuildInput()
{
	UEnhancedInputLocalPlayerSubsystem* Sub = IsLocalController() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()) : nullptr;
	if (Sub && InputSet)
	{
		Sub->RemoveMappingContext(InputSet->GetContext());
	}
	InputSet = nullptr; // rebuilt from the data rows plus the saved rebinds
	if (Sub)
	{
		Sub->AddMappingContext(GetInputSet()->GetContext(), 0);
	}
	// The pawn's bound actions are found by name in the set: rebind them to the new objects.
	if (ADCCharacter* Me = Cast<ADCCharacter>(GetPawn()))
	{
		Me->RebindInput();
	}
}

FString ADCPlayerController::SettingsValue(FName Row) const
{
	const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	if (!Save)
	{
		return FString();
	}
	const FDCSettingsSave& S = Save->Settings;
	static const TCHAR* QualityNames[] = {TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH")};
	const TMap<FName, FString> Values = {
		{TEXT("Volume"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(S.MasterVolume * 100.f))},
		{TEXT("Mouse"), FString::Printf(TEXT("%.2f"), GetSensitivity())},
		{TEXT("Quality"), QualityNames[FMath::Clamp(S.Quality, 0, 2)]},
		{TEXT("Window"), S.bFullscreen ? FString(TEXT("FULLSCREEN")) : FString(TEXT("WINDOWED"))},
	};
	const FString* V = Values.Find(Row);
	return V ? *V : FString();
}

void ADCPlayerController::OpenBuilder()
{
	UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	BuilderSlot = Save ? FMath::Clamp(Save->Characters.ActiveSlot, 0, UDCTuning::Get().Run.CharacterSlots - 1) : 0;
	BuilderSelectSlot(BuilderSlot);
	if (!OutfitPreview)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OutfitPreview = GetWorld()->SpawnActor<ADCOutfitPreview>(ADCOutfitPreview::StaticClass(), FTransform(UDCTuning::Get().Run.PreviewLocation), Params);
	}
	bBuilderOpen = true;
	BuilderChanged(TEXT(""));
}

void ADCPlayerController::CloseBuilder()
{
	bBuilderOpen = false;
	if (OutfitPreview)
	{
		OutfitPreview->Destroy();
		OutfitPreview = nullptr;
	}
	MenuMode = 254; // rebuild the main menu
}

UTextureRenderTarget2D* ADCPlayerController::GetBuilderImage() const
{
	return OutfitPreview ? OutfitPreview->GetImage() : nullptr;
}

float ADCPlayerController::GetBuilderImageAlpha() const
{
	return OutfitPreview ? OutfitPreview->ImageAlpha() : 0.f;
}

void ADCPlayerController::BuilderSelectSlot(int32 Slot)
{
	const UDCTuning& T = UDCTuning::Get();
	BuilderSlot = FMath::Clamp(Slot, 0, FMath::Max(T.Run.CharacterSlots, 1) - 1);
	UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	const FString Code = Save && Save->Characters.Slots.IsValidIndex(BuilderSlot) ? Save->Characters.Slots[BuilderSlot] : FString();
	BuilderOutfit = Code.IsEmpty() ? DCOutfit::Default(T.OutfitSlots) : DCOutfitBuild::FromCode(Code);
	if (bBuilderOpen)
	{
		BuilderChanged(FString::Printf(TEXT("SLOT %d"), BuilderSlot + 1));
	}
}

void ADCPlayerController::BuilderCycle(int32 SlotIndex, int32 Step)
{
	const UDCTuning& T = UDCTuning::Get();
	DCOutfit::Cycle(BuilderOutfit, SlotIndex, Step, T.OutfitSlots, T.OutfitParts);
	BuilderChanged(TEXT(""));
}

void ADCPlayerController::BuilderSwatch(bool bMain, int32 Step)
{
	uint8& Which = bMain ? BuilderOutfit.Main : BuilderOutfit.Accent;
	Which = DCOutfit::Wrap(Which, Step, UDCTuning::Get().OutfitSwatches.Num());
	BuilderChanged(TEXT(""));
}

void ADCPlayerController::BuilderGlow(int32 Step)
{
	BuilderOutfit.Glow = DCOutfit::Wrap(BuilderOutfit.Glow, Step, UDCTuning::Get().OutfitGlows.Num());
	BuilderChanged(TEXT(""));
}

void ADCPlayerController::BuilderPreset(int32 Step)
{
	const UDCTuning& T = UDCTuning::Get();
	if (T.OutfitPresets.IsEmpty())
	{
		return;
	}
	// The first press lands on the first (or last) preset; later presses step from the one worn.
	if (BuilderPresetIndex < 0)
	{
		BuilderPresetIndex = Step > 0 ? -1 : 0;
	}
	BuilderPresetIndex = DCOutfit::Wrap(BuilderPresetIndex, Step, T.OutfitPresets.Num());
	const FDCOutfitPreset& Preset = T.OutfitPresets[BuilderPresetIndex];
	FDCOutfit Worn;
	FString Error;
	if (!DCOutfit::FromPreset(Preset, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches, T.OutfitGlows, Worn, Error))
	{
		BuilderMessage = Error.ToUpper();
		MenuMode = 254;
		return;
	}
	BuilderOutfit = Worn;
	BuilderChanged(FString::Printf(TEXT("PRESET: %s"), *Preset.DisplayName.ToString().ToUpper()));
}

void ADCPlayerController::BuilderRandom()
{
	const UDCTuning& T = UDCTuning::Get();
	FRandomStream Rng(static_cast<int32>(FPlatformTime::Cycles()));
	BuilderOutfit = DCOutfit::Random(Rng, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num());
	BuilderPresetIndex = -1;
	BuilderChanged(TEXT("RANDOM PHOTOGRAPHER"));
}

FString ADCPlayerController::GetBuilderPresetName() const
{
	// Named only while the outfit is still that preset: any part, swatch or glow change (or a loaded code) clears it.
	const UDCTuning& T = UDCTuning::Get();
	if (!T.OutfitPresets.IsValidIndex(BuilderPresetIndex))
	{
		return FString();
	}
	const FDCOutfitPreset& Preset = T.OutfitPresets[BuilderPresetIndex];
	FDCOutfit Worn;
	FString Error;
	const bool bStill = DCOutfit::FromPreset(Preset, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches, T.OutfitGlows, Worn, Error) && Worn == BuilderOutfit;
	return bStill ? Preset.DisplayName.ToString() : FString();
}

void ADCPlayerController::BuilderImport(const FString& Code)
{
	FDCOutfit Loaded;
	FString Error;
	if (!DCOutfitBuild::Decode(Code, Loaded, Error))
	{
		BuilderMessage = FString::Printf(TEXT("CODE NOT LOADED: %s"), *Error.ToUpper());
		MenuMode = 254;
		return;
	}
	BuilderOutfit = Loaded;
	BuilderChanged(TEXT("FRIEND'S CODE LOADED"));
}

void ADCPlayerController::BuilderCopyCode()
{
	FPlatformApplicationMisc::ClipboardCopy(*DCOutfit::Encode(BuilderOutfit));
	BuilderMessage = TEXT("CODE COPIED");
	MenuMode = 254;
}

void ADCPlayerController::BuilderExportCard()
{
	// R5: the shareable ID card, rendered from the builder's own turntable (the same outfit recipe as in play).
	if (!OutfitPreview)
	{
		return;
	}
	const ADCPlayerState* PS = GetPlayerState<ADCPlayerState>();
	const FString Sign = PS ? PS->GetPlayerName() : FString(TEXT("CORRESPONDENT"));
	const FString Path = OutfitPreview->ExportCard(Sign, DCOutfit::Encode(BuilderOutfit));
	BuilderChanged(Path.IsEmpty() ? FString(TEXT("ID CARD FAILED")) : FString::Printf(TEXT("ID CARD SAVED: %s"), *Path));
}

void ADCPlayerController::BuilderChanged(const FString& Message)
{
	BuilderMessage = Message;
	if (UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this))
	{
		Save->Characters.Slots.SetNum(FMath::Max(Save->Characters.Slots.Num(), BuilderSlot + 1));
		Save->Characters.Slots[BuilderSlot] = DCOutfit::Encode(BuilderOutfit);
		Save->Characters.ActiveSlot = BuilderSlot;
		Save->SaveCharacters();
	}
	if (OutfitPreview)
	{
		OutfitPreview->Show(BuilderOutfit);
	}
	MenuMode = 254; // the menu rebuilds with the new names and code
}

void ADCPlayerController::TickAlbum()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || GS->Phase != EDCRunPhase::FrontPage)
	{
		bAlbumSaved = false;
		return;
	}
	UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(this);
	if (bAlbumSaved || !Save || GS->PagePicks.Num() == 0)
	{
		return;
	}
	bAlbumSaved = true;
	const APlayerState* Me = GetPlayerState<APlayerState>();
	TArray<const FDCPagePick*> Picks;
	for (const FDCPagePick& P : GS->PagePicks)
	{
		Picks.Add(&P);
	}
	Picks.StableSort([](const FDCPagePick& A, const FDCPagePick& B) { return A.PageScore > B.PageScore; });
	int32 Added = 0;
	for (const FDCPagePick* P : Picks)
	{
		const FDCLocalPhoto* Mine = Me && P->Photo.OwnerPlayerId == Me->GetPlayerId() ? Album.FindByPredicate([P](const FDCLocalPhoto& L) { return L.PhotoId == P->Photo.PhotoId; }) : nullptr;
		if (Mine && Mine->Pixels.Num() > 0 && Added < UDCTuning::Get().Run.AlbumPicksPerRun)
		{
			Save->AddToAlbum(TEXT("photo"), Mine->Pixels, Mine->Width, Mine->Height, P->PageScore, P->Headline.IsEmpty() ? GS->Headline : P->Headline, P->PlayerName);
			++Added;
		}
	}
	// The page itself, once it has drawn: a screenshot into the album folder.
	const FString PageFile = Save->AddFrontPageToAlbum(GS->Headline, Me ? Me->GetPlayerName() : FString());
	FTimerHandle Later;
	GetWorldTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(this, [PageFile] { FScreenshotRequest::RequestScreenshot(PageFile, true, false); }), 1.5f, false);
	UE_LOG(LogTemp, Display, TEXT("DCALBUM front page: %d own picks saved, page shot %s"), Added, *PageFile);
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

void ADCPlayerController::TickPerf()
{
	static const bool bPerf = FParse::Param(FCommandLine::Get(), TEXT("dcperf"));
	if (!bPerf)
	{
		return;
	}
	const float FrameMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
	PerfFrameMs.Add(FrameMs);
	PerfGameMsSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
	PerfRenderMsSum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	if (FrameMs > 50.f)
	{
		// Which thread stalled: game, render, GPU (previous frame's timings, the closest the engine keeps). Advisor a-25:
		// every spike classifies itself (run phase, city loading, falling pieces, live units, viewport, seconds since GC).
		static double LastGc = -1.0;
		static const bool bGcHooked = [] { FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([] { LastGc = FPlatformTime::Seconds(); }); return true; }();
		const ADCGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADCGameState>() : nullptr;
		const ADCVoxelCity* SpikeCity = ADCVoxelCity::Get(this);
		const UDCUnits* Units = UDCUnits::Get(this);
		FVector2D View = FVector2D::ZeroVector;
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(View);
		}
		UE_LOG(LogTemp, Display, TEXT("DCSPIKE frame_ms=%.1f game_ms=%.1f render_ms=%.1f gpu_ms=%.1f phase=%d loading=%d falling=%d units=%d view=%.0fx%.0f since_gc=%.1f"), FrameMs, FPlatformTime::ToMilliseconds(GGameThreadTime),
			FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), GS ? static_cast<int32>(GS->Phase) : -1,
			SpikeCity && (SpikeCity->IsCrafting() || !SpikeCity->IsLoaded()) ? 1 : 0, SpikeCity ? SpikeCity->GetFallingCount() : 0, Units ? Units->All().Num() : 0,
			View.X, View.Y, bGcHooked && LastGc > 0.0 ? FPlatformTime::Seconds() - LastGc : -1.0);
	}
	if (const ADCVoxelCity* City = ADCVoxelCity::Get(this))
	{
		PerfMaxFalling = FMath::Max(PerfMaxFalling, City->GetFallingCount());
		// Load phase (advisor CP15: rows carry the phase): any frame of the second with the sector still crafting or
		// uploading under the sector card.
		bPerfLoading = bPerfLoading || City->IsCrafting() || !City->IsLoaded();
	}
	const double Now = FPlatformTime::Seconds();
	if (PerfWindowStart <= 0.0)
	{
		PerfWindowStart = Now;
		return;
	}
	if (Now - PerfWindowStart < 1.0 || PerfFrameMs.Num() == 0)
	{
		return;
	}
	TArray<float> Sorted = PerfFrameMs;
	Sorted.Sort();
	float Sum = 0.f;
	for (const float Ms : Sorted)
	{
		Sum += Ms;
	}
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const float GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	const FIntPoint View = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	UE_LOG(LogTemp, Display, TEXT("DCPERF frames=%d avg_ms=%.2f p95_ms=%.2f max_ms=%.2f gpu_ms=%.2f game_ms=%.2f render_ms=%.2f players=%d falling_max=%d phase=%d loading=%d view=%dx%d"),
		Sorted.Num(), Sum / Sorted.Num(), Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt32(Sorted.Num() * 0.95f))], Sorted.Last(), GpuMs,
		PerfGameMsSum / Sorted.Num(), PerfRenderMsSum / Sorted.Num(),
		GS ? GS->PlayerArray.Num() : 0, PerfMaxFalling, GS ? static_cast<int32>(GS->Phase) : -1, bPerfLoading ? 1 : 0, View.X, View.Y);
	PerfFrameMs.Reset();
	PerfGameMsSum = PerfRenderMsSum = 0.0;
	PerfMaxFalling = 0;
	bPerfLoading = false;
	PerfWindowStart = Now;
}

void ADCPlayerController::TickBuilderFlow()
{
	// -dcbuilderflow=<dir>: open the photographer builder, step parts and swatches, load a share code; a frame each.
	static FString Dir;
	static const bool bFlow = FParse::Value(FCommandLine::Get(), TEXT("dcbuilderflow="), Dir);
	static FString SettingsDir;
	static const bool bSettingsFlow = FParse::Value(FCommandLine::Get(), TEXT("dcsettingsflow="), SettingsDir);
	if (!bFlow && !bSettingsFlow)
	{
		return;
	}
	static int32 Step = 0;
	const double T = FPlatformTime::Seconds() - GStartTime;
	auto Shot = [](const FString& Name) { FScreenshotRequest::RequestScreenshot((bSettingsFlow ? SettingsDir : Dir) / Name, true, false); };
	// -dcsettingsflow=<dir>: open settings, turn the volume down and the quality to medium, frame, back.
	const TArray<TPair<double, TFunction<void()>>> SettingsSteps = {
		{5.0, [this]() { OpenSettings(); }},
		{6.5, [&]() { Shot(TEXT("01_settings_open.png")); }},
		{7.0, [this]() { SettingsStep(TEXT("Volume"), -1); SettingsStep(TEXT("Volume"), -1); SettingsStep(TEXT("Quality"), -1); SettingsStep(TEXT("Mouse"), 1); }},
		{8.5, [&]() { Shot(TEXT("02_settings_changed_and_saved.png")); }},
		{9.0, [this]() { BeginRebind(TEXT("Jump")); }},
		{9.8, [&]() { Shot(TEXT("03_rebind_waiting_for_key.png")); }},
		{10.2, [this]() { InputKey(FInputKeyEventArgs(GetWorld()->GetGameViewport() ? GetWorld()->GetGameViewport()->Viewport : nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::J, IE_Pressed, FPlatformTime::Cycles64())); }},
		{11.0, [&]() { Shot(TEXT("04_jump_rebound_to_J.png")); }},
		{11.5, [this]() { CloseSettings(); }},
		{12.5, [&]() { Shot(TEXT("05_back_to_menu.png")); }},
		// The saved binding in play (critic CP6): start a run and press the rebound key; the photographer jumps.
		{13.0, [this]() { MenuSolo(); }},
		{22.0, [&, this]() { Shot(TEXT("06_street_before_J.png")); UE_LOG(LogTemp, Display, TEXT("DCSETTINGSFLOW before J: pawn z=%.0f"), GetPawn() ? GetPawn()->GetActorLocation().Z : -1.f); }},
		{22.3, [this]() { InputKey(FInputKeyEventArgs(GetWorld()->GetGameViewport() ? GetWorld()->GetGameViewport()->Viewport : nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::J, IE_Pressed, FPlatformTime::Cycles64())); }},
		{22.55, [&, this]() { Shot(TEXT("07_J_pressed_photographer_in_the_air.png")); UE_LOG(LogTemp, Display, TEXT("DCSETTINGSFLOW 0.25 s after J: pawn z=%.0f falling=%d"), GetPawn() ? GetPawn()->GetActorLocation().Z : -1.f, GetCharacter() && GetCharacter()->GetCharacterMovement() ? (GetCharacter()->GetCharacterMovement()->IsFalling() ? 1 : 0) : -1); }},
		{22.7, [this]() { InputKey(FInputKeyEventArgs(GetWorld()->GetGameViewport() ? GetWorld()->GetGameViewport()->Viewport : nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::J, IE_Released, FPlatformTime::Cycles64())); }},
		{23.5, [this]() { UE_LOG(LogTemp, Display, TEXT("DCSETTINGSFLOW J press done, pawn z=%.0f"), GetPawn() ? GetPawn()->GetActorLocation().Z : -1.f); }},
		{24.0, []() { FGenericPlatformMisc::RequestExit(false); }},
	};
	// Shots 6 s after opening: an editor -game run compiles the voxel preview shaders on demand first.
	TArray<TPair<double, TFunction<void()>>> BuilderSteps = {
		{5.0, [this]() { OpenBuilder(); }},
		{11.0, [&]() { Shot(TEXT("01_builder_default.png")); }},
		{12.5, [&]() { Shot(TEXT("02_builder_turned.png")); }},
		{13.0, [this]() { BuilderCycle(0, 1); BuilderCycle(1, 1); BuilderSwatch(true, 2); BuilderSwatch(false, 4); }},
		{14.5, [&]() { Shot(TEXT("03_builder_helmet_trench.png")); }},
		{15.0, [this]() { BuilderCycle(2, 1); BuilderCycle(3, 1); BuilderSwatch(true, 1); }},
		{16.5, [&]() { Shot(TEXT("04_builder_filmbelt_boxcam.png")); }},
	};
	// R18 evidence, from the data: every part of every slot worn (a frame each), every preset, two random outfits, the
	// glow colours, and an old DC1 code loading the same outfit.
	double At = 16.5;
	auto Then = [&](double Gap, TFunction<void()> Fn) { At += Gap; BuilderSteps.Add({At, MoveTemp(Fn)}); };
	const UDCTuning& Tune = UDCTuning::Get();
	// The turntable holds still for these frames: three-quarter front, gear from behind (it is worn on the back).
	const TMap<FName, float> SlotYaw = {{TEXT("Head"), 25.f}, {TEXT("Body"), 25.f}, {TEXT("Gear"), 145.f}, {TEXT("Camera"), 5.f}};
	auto Face = [this](float Yaw) { if (OutfitPreview) { OutfitPreview->Face(Yaw); } };
	// The turntable's own render target as well: window screenshots drop out when the window stops presenting.
	auto Turntable = [this](const FString& Name) { if (OutfitPreview) { OutfitPreview->SaveImage(Dir / TEXT("turntable") / Name); } };
	for (int32 Slot = 0; Slot < Tune.OutfitSlots.Num(); ++Slot)
	{
		const TArray<const FDCOutfitPart*> Parts = DCOutfit::PartsOf(Tune.OutfitSlots[Slot], Tune.OutfitParts);
		const float Yaw = SlotYaw.FindRef(Tune.OutfitSlots[Slot]);
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString Name = FString::Printf(TEXT("r18_%s_%02d_%s.png"), *Tune.OutfitSlots[Slot].ToString().ToLower(), i, *Parts[i]->Id.ToString().ToLower());
			Then(0.4, [this, Slot, i, Yaw, Face]() { BuilderOutfit.Parts[Slot] = static_cast<uint8>(i); BuilderOutfit.Glow = static_cast<uint8>(Slot % FMath::Max(UDCTuning::Get().OutfitGlows.Num(), 1)); BuilderChanged(TEXT("")); Face(Yaw); });
			Then(1.2, [&, Name]() { Shot(Name); Turntable(Name); });
		}
	}
	Then(0.1, [Face]() { Face(25.f); });
	for (int32 i = 0; i < Tune.OutfitPresets.Num(); ++i)
	{
		const FString Name = FString::Printf(TEXT("r18_preset_%s.png"), *Tune.OutfitPresets[i].Id.ToString().ToLower());
		Then(0.4, [this]() { BuilderPreset(1); UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW preset %s: %s"), *GetBuilderPresetName(), *DCOutfit::Encode(BuilderOutfit)); });
		Then(1.2, [&, Name]() { Shot(Name); Turntable(Name); });
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const FString Name = FString::Printf(TEXT("r18_random_%d.png"), i + 1);
		Then(0.4, [this]() { BuilderRandom(); UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW random: %s"), *DCOutfit::Encode(BuilderOutfit)); });
		Then(1.2, [&, Name]() { Shot(Name); Turntable(Name); });
	}
	for (int32 i = 0; i < Tune.OutfitGlows.Num(); ++i)
	{
		const FString Name = FString::Printf(TEXT("r18_glow_%s.png"), *Tune.OutfitGlows[i].Id.ToString().ToLower());
		Then(0.4, [this, i]() { BuilderPresetIndex = 0; BuilderPreset(1); BuilderOutfit.Glow = static_cast<uint8>(i); BuilderChanged(TEXT("")); });
		Then(1.2, [&, Name]() { Shot(Name); Turntable(Name); });
	}
	Then(0.4, [this]()
	{
		// An old (R23) share code still loads the outfit it was made for.
		BuilderImport(TEXT("DC1-04010-20000-0503F"));
		UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW old code DC1-04010-20000-0503F -> %s (%s)"), *DCOutfit::Encode(BuilderOutfit), *BuilderMessage);
	});
	Then(1.2, [&]() { Shot(TEXT("r18_old_code_loaded.png")); Turntable(TEXT("r18_old_code_loaded.png")); });
	Then(0.1, [Face]() { Face(-1.f); });
	Then(0.4, [this]()
	{
		// A friend's photographer: another valid outfit (every slot cycled, other swatches), encoded and pasted back.
		const UDCTuning& T = UDCTuning::Get();
		FDCOutfit Friend = DCOutfit::Default(T.OutfitSlots);
		for (int32 Slot = 0; Slot < T.OutfitSlots.Num(); ++Slot)
		{
			DCOutfit::Cycle(Friend, Slot, Slot + 1, T.OutfitSlots, T.OutfitParts);
		}
		Friend.Main = DCOutfit::Wrap(BuilderOutfit.Main, 2, T.OutfitSwatches.Num());
		Friend.Accent = DCOutfit::Wrap(BuilderOutfit.Accent, 3, T.OutfitSwatches.Num());
		const FString Code = DCOutfit::Encode(Friend);
		UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW friend code %s"), *Code);
		BuilderImport(Code);
		UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW after import: %s (%s)"), *DCOutfit::Encode(BuilderOutfit), *BuilderMessage);
	});
	Then(1.5, [&]() { Shot(TEXT("05_builder_code_loaded.png")); });
	Then(0.5, [this]() { BuilderImport(TEXT("DC1-NOT-A-REAL-CODE")); });
	Then(0.8, [&]() { Shot(TEXT("06_builder_bad_code_rejected.png")); });
	// R5 + R18: the ID card of an outfit made of new parts (the second preset: visor, hoodie, drone, drone camera).
	Then(0.4, [this]() { BuilderPresetIndex = 0; BuilderPreset(1); });
	Then(1.5, [this]() { BuilderExportCard(); UE_LOG(LogTemp, Display, TEXT("DCBUILDERFLOW card: %s"), *BuilderMessage); });
	Then(0.8, [&]() { Shot(TEXT("07_builder_id_card_exported.png")); });
	// R5 parity: the same outfit in play (DCOUTFIT ... wears <code> matches the card's code).
	Then(0.5, [this]() { CloseBuilder(); });
	Then(1.0, [this]() { MenuSolo(); });
	// After the sector title card (it covers the street for its first seconds).
	Then(17.0, [&]() { Shot(TEXT("08_same_outfit_in_play.png")); });
	Then(1.0, []() { FGenericPlatformMisc::RequestExit(false); });
	const TArray<TPair<double, TFunction<void()>>>& Steps = bSettingsFlow ? SettingsSteps : BuilderSteps;
	if (Steps.IsValidIndex(Step) && T >= Steps[Step].Key)
	{
		Steps[Step].Value();
		++Step;
	}
}

void ADCPlayerController::TickMenuFlow()
{
	static FString Dir;
	static const bool bFlow = FParse::Value(FCommandLine::Get(), TEXT("dcmenuflow="), Dir);
	if (!bFlow || !FSlateApplication::IsInitialized())
	{
		return;
	}
	// Steps run once each, in order, on real seconds since start (the travel replaces this controller; state is static).
	static int32 Step = 0;
	const double T = FPlatformTime::Seconds() - GStartTime;
	FSlateApplication& App = FSlateApplication::Get();
	auto Shot = [](const FString& Name) { FScreenshotRequest::RequestScreenshot(Dir / Name, true, false); };
	auto ButtonCentre = []() -> TOptional<FVector2D>
	{
		const TSharedPtr<SWidget> B = DCMenu::SoloButton().Pin();
		if (!B.IsValid())
		{
			return {};
		}
		return FVector2D(B->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f)));
	};
	auto RetryClick = [&]() -> bool
	{
		const ADCGameState* G = GetWorld()->GetGameState<ADCGameState>();
		const TOptional<FVector2D> At = ButtonCentre();
		if (!G || G->Phase != EDCRunPhase::Lobby || !At)
		{
			return true; // the run started (or the menu is gone): nothing to retry
		}
		UE_LOG(LogTemp, Display, TEXT("DCMENUFLOW click SOLO RUN again at %s (still in the lobby)"), *At->ToString());
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
		{
			GEngine->GameViewport->GetWindow()->BringToFront(true); // the OS gave the first click to another window
		}
		App.SetCursorPos(*At);
		App.ProcessMouseMoveEvent(FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
		App.ProcessMouseButtonUpEvent(FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
		return true;
	};
	using FFlowStep = TFunction<bool()>;
	const TArray<TPair<double, FFlowStep>> Steps = { // rebuilt each tick: the steps capture this tick's locals
		{6.0, [&]() { Shot(TEXT("01_menu.png")); return true; }},
		{7.0, [&]()
			{
				const TOptional<FVector2D> At = ButtonCentre();
				if (!At)
				{
					return false;
				}
				App.SetCursorPos(*At);
				App.ProcessMouseMoveEvent(FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
				return true;
			}},
		{7.6, [&]() { Shot(TEXT("02_menu_cursor_on_solo_run.png")); return true; }},
		{8.2, [&]()
			{
				const TOptional<FVector2D> At = ButtonCentre();
				if (!At)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("DCMENUFLOW click SOLO RUN at %s"), *At->ToString());
				App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
				App.ProcessMouseButtonUpEvent(FPointerEvent(App.GetUserIndexForMouse(), *At, *At, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
				return true;
			}},
		{8.3, [&]() { Shot(TEXT("03_after_click.png")); return true; }},
		// A click the OS routed to another window (the first click only activates ours) is clicked again while the
		// menu is still up: still a real Slate move + press + release on the button.
		{10.5, [&]() { return RetryClick(); }},
		{12.5, [&]() { return RetryClick(); }},
		{14.5, [this]() // still in the lobby after three real clicks: start the run anyway and say so in the log
			{
				const ADCGameState* G = GetWorld()->GetGameState<ADCGameState>();
				if (G && G->Phase == EDCRunPhase::Lobby)
				{
					UE_LOG(LogTemp, Warning, TEXT("DCMENUFLOW fallback: three clicks lost to window focus, starting the run directly"));
					MenuSolo();
				}
				return true;
			}},
		{16.0, [&]() { Shot(TEXT("04_run_started_street.png")); return true; }},
		{21.0, [&]() { Shot(TEXT("05_street_5s_later.png")); return true; }},
		// With -dcdemo the pilot plays on from the street (R14: one timed run from the menu to the front page).
		{22.0, [&]() { if (!FParse::Param(FCommandLine::Get(), TEXT("dcdemo"))) { FGenericPlatformMisc::RequestExit(false); } return true; }},
	};
	if (Steps.IsValidIndex(Step) && T >= Steps[Step].Key && Steps[Step].Value())
	{
		++Step;
	}
}

void ADCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (IsLocalController())
	{
		TickPerf();
		TickMenuFlow();
		TickBuilderFlow();
		TickAlbum();
		if (!bOutfitSent && GetPlayerState<ADCPlayerState>())
		{
			bOutfitSent = true;
			SendOutfit(); // R23: the saved photographer, validated and replicated by the host
		}
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
	const bool bWant = GS && GS->Phase == EDCRunPhase::Lobby && (!FParse::Param(FCommandLine::Get(), TEXT("dcdemo")) || FCString::Strifind(FCommandLine::Get(), TEXT("dcmenuflow=")));
	const uint8 Base = MenuModeFor(*this);
	const uint8 Mode = !bWant ? 255 : (Base == 0 && bBuilderOpen ? 3 : (Base == 0 && bSettingsOpen ? 4 : Base));
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
	const bool bCursor = GS && WantsCursor(GS->Phase) && (!FParse::Param(FCommandLine::Get(), TEXT("dcdemo")) || (GS->Phase == EDCRunPhase::Lobby && FCString::Strifind(FCommandLine::Get(), TEXT("dcmenuflow="))));
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

void ADCPlayerController::ServerReelStaged_Implementation(int32 ClipIndex)
{
	if (ADCGameMode* GM = GetWorld()->GetAuthGameMode<ADCGameMode>())
	{
		GM->NoteReelStaged(this, ClipIndex);
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
	if (!GS || !PS || GS->Phase != EDCRunPhase::Reel || GS->ReelIndex != ClipIndex || GS->ReelStage > 1 || !GS->bReelClipLive || SecondTakeClips.Contains(ClipIndex) || PS->FindPhoto(PhotoId)
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
	const FDCPrintTuning& PrintT = UDCTuning::Get().Camera.Print;
	bool bRead = false;
	if (Resource && Image->RenderTargetFormat == RTF_RGBA16f)
	{
		// Float capture (advisor CP21): metered and developed before the print, no 8-bit stretch of a dark frame.
		TArray<FLinearColor> Linear;
		bRead = Resource->ReadLinearColorPixels(Linear) && Linear.Num() == Image->SizeX * Image->SizeY;
		if (bRead)
		{
			DCNewsprint::Develop(Linear, Image->SizeX, Image->SizeY, PrintT, Photo.Pixels);
		}
	}
	else if (Resource)
	{
		bRead = Resource->ReadPixels(Photo.Pixels);
	}
	if (bRead && Photo.Pixels.Num() == Image->SizeX * Image->SizeY)
	{
		Photo.Width = Image->SizeX;
		Photo.Height = Image->SizeY;
		DCNewsprint::Print(Photo.Pixels, Photo.Width, Photo.Height, PrintT, static_cast<uint32>(PhotoId));
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
