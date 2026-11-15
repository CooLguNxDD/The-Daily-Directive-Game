#include "DCMenu.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rules/DCOutfit.h"
#include "DCFonts.h"
#include "DCPalette.h"

#include "DCAudio.h"
#include "DCGameState.h"
#include "DCLanBeacon.h"
#include "DCOnline.h"
#include "DCPlayerController.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Input/SButton.h"
#include "DCSave.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Engine/Texture2D.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

using namespace DCPalette;

namespace DCMenuStyle
{

	// Print type (R7): big sizes set in the headline face, the rest in the body face.
	FSlateFontInfo Font(int32 Size, const char* /*Weight*/ = "Bold")
	{
		return DCFonts::Slate(Size >= 40 ? TEXT("Headline") : TEXT("Body"), Size);
	}

	TSharedRef<SWidget> Label(const FString& Text, int32 Size, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Color).Justification(ETextJustify::Center).AutoWrapText(false);
	}

	/** Dark-glass buttons (R25, V9.1: no solid yellow plates): a glass fill with a thin edge in the accent colour and
	 *  light type; hover lights the glass with the accent and thickens the edge, press fills it further. One style per
	 *  accent, kept for the session (Slate holds the style by pointer). */
	const FButtonStyle& GlassButton(const FLinearColor& Accent)
	{
		static TMap<uint32, TUniquePtr<FButtonStyle>> Styles;
		const uint32 Key = GetTypeHash(Accent.ToFColor(false));
		if (const TUniquePtr<FButtonStyle>* Found = Styles.Find(Key))
		{
			return **Found;
		}
		const FLinearColor Glass(Paper.R, Paper.G, Paper.B, 0.92f);
		const FLinearColor Edge(Accent.R, Accent.G, Accent.B, 0.85f);
		FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		S.SetNormal(FSlateRoundedBoxBrush(Glass, 0.f, Edge, 2.f));
		S.SetHovered(FSlateRoundedBoxBrush(FLinearColor(Accent.R * 0.22f, Accent.G * 0.22f, Accent.B * 0.22f, 0.95f), 0.f, Accent, 4.f));
		S.SetPressed(FSlateRoundedBoxBrush(FLinearColor(Accent.R * 0.4f, Accent.G * 0.4f, Accent.B * 0.4f, 0.95f), 0.f, Accent, 5.f));
		S.SetNormalPadding(FMargin(0));
		S.SetPressedPadding(FMargin(0));
		return *Styles.Add(Key, MakeUnique<FButtonStyle>(S));
	}

	TWeakPtr<SWidget> GSoloButton;

	TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> OnClick, const FLinearColor& Fill = Gold, TWeakPtr<SWidget>* OutButton = nullptr)
	{
		TSharedPtr<SButton> Made;
		TSharedRef<SWidget> Box = SNew(SBox).WidthOverride(420.f).HeightOverride(64.f).Padding(FMargin(0, 6))
			[
				SAssignNew(Made, SButton)
				.ButtonStyle(&GlassButton(Fill))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(22)).ColorAndOpacity(Fill.GetLuminance() > 0.5f ? Ink : Fill) // light type on glass
				]
			];
		if (OutButton)
		{
			*OutButton = Made;
		}
		return Box;
	}

	FString LocalAddress()
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		bool bCanBind = false;
		TSharedPtr<FInternetAddr> Addr;
		if (Sockets)
		{
			Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind);
		}
		return Addr.IsValid() ? Addr->ToString(false) : TEXT("your LAN address");
	}
}

namespace DCMenu
{
	using namespace DCMenuStyle;

	TSharedRef<SWidget> Build(ADCPlayerController* PC, uint8 Mode, const TArray<FDCLanHost>& LanHosts)
	{
		TWeakObjectPtr<ADCPlayerController> Weak(PC);
		auto Click = [Weak](void (ADCPlayerController::*Fn)())
		{
			return [Weak, Fn]()
			{
				if (ADCPlayerController* P = Weak.Get())
				{
					DCAudio::Play2D(P, TEXT("UIClick"));
					(P->*Fn)();
				}
			};
		};

		TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
		// The game is THE DAILY DIRECTIVE (human 15:15): the title in the newspaper's blackletter masthead face.
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
			[SNew(STextBlock).Text(FText::FromString(TEXT("The Daily Directive"))).Font(DCFonts::Slate(TEXT("Masthead"), 60)).ColorAndOpacity(Ink).Justification(ETextJustify::Center)];
		// The builder (mode 3, R18) needs the height for its rows: no tagline and no key hints there.
		const bool bBuilderPage = Mode == 3;
		if (!bBuilderPage)
		{
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 24)
				[Label(TEXT("Call the strike. Run toward it. Snap the collapse at peak bloom.\nYour photos buy bigger bombs. The best one makes the front page."), 16, Ink)];
		}

		const uint16 Port = static_cast<uint16>(UDCTuning::Get().Run.Port);
		// One builder per menu mode, picked by index.
		const TFunction<void()> Modes[] = {
		[&]()
		{
			TSharedRef<SEditableTextBox> Address = SNew(SEditableTextBox).Text(FText::FromString(TEXT("127.0.0.1"))).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("SOLO RUN"), Click(&ADCPlayerController::MenuSolo), Gold, &GSoloButton)];
			{
				// R25: until the page has been opened once (saved), the button shines: a pulsing glow in the noir accent.
				const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(PC);
				const bool bShine = Save && !Save->Settings.bHowToPlaySeen;
				const FLinearColor Glow = UDCTuning::Get().Look.HowToShineColor;
				const float Hz = UDCTuning::Get().Look.HowToShineHz;
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
					[SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.Padding(FMargin(bShine ? 4.f : 0.f))
					.BorderBackgroundColor_Lambda([bShine, Glow, Hz]()
						{
							const float A = bShine ? 0.35f + 0.65f * (0.5f + 0.5f * FMath::Sin(FPlatformTime::Seconds() * UE_TWO_PI * Hz)) : 0.f;
							return FSlateColor(FLinearColor(Glow.R, Glow.G, Glow.B, A));
						})
					[Button(TEXT("HOW TO PLAY"), Click(&ADCPlayerController::OpenHowTo), bShine ? Glow : Gold)]];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("HOST CO-OP (1-4)"), Click(&ADCPlayerController::MenuHost))];
			// R26 internet co-op (behind the online switch): host over EOS with a join code, or join by a friend's code.
			UDCOnline* Online = UDCOnline::Get(PC);
			if (Online && Online->IsSwitchOn())
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("HOST ONLINE (JOIN CODE)"), Click(&ADCPlayerController::MenuHostOnline))];
				TSharedRef<SEditableTextBox> CodeBox = SNew(SEditableTextBox).HintText(FText::FromString(TEXT("CODE"))).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(200.f).HeightOverride(44.f)[CodeBox]]
					+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("JOIN BY CODE"), [Weak, CodeBox]()
					{
						if (ADCPlayerController* P = Weak.Get())
						{
							DCAudio::Play2D(P, TEXT("UIClick"));
							P->MenuJoinCode(CodeBox->GetText().ToString());
						}
					})]
				];
			}
			if (Online && !Online->GetStatus().IsEmpty())
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Label(Online->GetStatus(), 13, Gold)];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("YOUR PHOTOGRAPHER"), Click(&ADCPlayerController::OpenBuilder))];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("SETTINGS"), Click(&ADCPlayerController::OpenSettings))];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)[Label(TEXT("JOIN A FRIEND: HOST IP"), 14, Ink)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(420.f).HeightOverride(44.f)[Address]];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("JOIN"), [Weak, Address]()
			{
				if (ADCPlayerController* P = Weak.Get())
				{
					DCAudio::Play2D(P, TEXT("UIClick"));
					P->MenuJoin(Address->GetText().ToString());
				}
			})];
			// LAN games heard on the local network (R12), one JOIN button each.
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)
				[Label(LanHosts.Num() > 0 ? TEXT("GAMES ON YOUR NETWORK") : TEXT("LOOKING FOR GAMES ON YOUR NETWORK..."), 14, Ink)];
			for (const FDCLanHost& Host : LanHosts)
			{
				const FString HostAddress = Host.Address;
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
					[Label(FString::Printf(TEXT("%s  at  %s"), *Host.Name.ToUpper(), *HostAddress), 12, Ink)];
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(FString::Printf(TEXT("JOIN  (%d/%d PLAYERS)"), Host.Players, UDCTuning::Get().Run.MaxPlayers), [Weak, HostAddress]()
				{
					if (ADCPlayerController* P = Weak.Get())
					{
						DCAudio::Play2D(P, TEXT("UIClick"));
						P->MenuJoin(HostAddress);
					}
				})];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)[Button(TEXT("QUIT"), Click(&ADCPlayerController::MenuQuit), Ink)];
		},
		[&]()
		{
			// R26: an EOS host has no IP listener, so its lobby shows only the join code.
			const UDCOnline* Online = UDCOnline::Get(PC);
			if (!(Online && Online->IsCarrying()))
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(FString::Printf(TEXT("HOSTING. FRIENDS JOIN: %s (port %d)"), *LocalAddress(), Port), 20, Ink)];
			}
			// R26: the online join code (or why the host fell back to the network).
			if (Online && !Online->GetStatus().IsEmpty())
			{
				const bool bCode = Online->GetState() == EDCOnlineState::Hosting && !Online->GetCode().IsEmpty() && Online->GetStatus().Contains(Online->GetCode());
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[Label(Online->GetStatus(), bCode ? 30 : 15, Gold)];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[Label(TEXT("Players in the lobby are listed below. Start when everyone is in."), 14, Ink)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("START RUN"), Click(&ADCPlayerController::RequestStartRun))];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("QUIT"), Click(&ADCPlayerController::MenuQuit), Ink)];
		},
		[&]()
		{
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(TEXT("CONNECTED. WAITING FOR THE HOST TO START THE RUN..."), 22, Ink)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("QUIT"), Click(&ADCPlayerController::MenuQuit), Ink)];
		},
		[&]()
		{
			// Photographer builder (R23, R18): one row per slot, swatch and glow rows, a preset row, save slots and a
			// random button, share code in and out.
			const UDCTuning& T = UDCTuning::Get();
			const FDCOutfit& O = PC->GetBuilderOutfit();
			auto Row = [&](const FString& Name, TFunction<void(int32)> Step)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(46.f)[Button(TEXT("<"), [Step]() { Step(-1); }, Gold)]]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(400.f)[Label(Name, 19, Ink)]]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(46.f)[Button(TEXT(">"), [Step]() { Step(1); }, Gold)]];
			};
			auto Act = [Weak](TFunction<void(ADCPlayerController&)> Fn)
			{
				return [Weak, Fn]()
				{
					if (ADCPlayerController* P = Weak.Get())
					{
						DCAudio::Play2D(P, TEXT("UIClick"));
						Fn(*P);
					}
				};
			};
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[Label(TEXT("YOUR PHOTOGRAPHER"), 30, Ink)];
			for (int32 s = 0; s < T.OutfitSlots.Num() && s < O.Parts.Num(); ++s)
			{
				const TArray<const FDCOutfitPart*> Parts = DCOutfit::PartsOf(T.OutfitSlots[s], T.OutfitParts);
				const FString Name = Parts.IsValidIndex(O.Parts[s]) ? Parts[O.Parts[s]]->DisplayName.ToString() : FString();
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
					[Row(FString::Printf(TEXT("%s: %s"), *T.OutfitSlots[s].ToString().ToUpper(), *Name.ToUpper()), [Act, s](int32 Step) { Act([s, Step](ADCPlayerController& P) { P.BuilderCycle(s, Step); })(); })];
			}
			auto SwatchName = [&T](uint8 I) { return T.OutfitSwatches.IsValidIndex(I) ? T.OutfitSwatches[I].Id.ToString().ToUpper() : FString(); };
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Row(TEXT("MAIN: ") + SwatchName(O.Main), [Act](int32 Step) { Act([Step](ADCPlayerController& P) { P.BuilderSwatch(true, Step); })(); })];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Row(TEXT("ACCENT: ") + SwatchName(O.Accent), [Act](int32 Step) { Act([Step](ADCPlayerController& P) { P.BuilderSwatch(false, Step); })(); })];
			const FString GlowName = T.OutfitGlows.IsValidIndex(O.Glow) ? T.OutfitGlows[O.Glow].Id.ToString().ToUpper() : FString();
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Row(TEXT("GLOW: ") + GlowName, [Act](int32 Step) { Act([Step](ADCPlayerController& P) { P.BuilderGlow(Step); })(); })];
			const FString PresetName = PC->GetBuilderPresetName().IsEmpty() ? FString(TEXT("PICK A LOOK")) : PC->GetBuilderPresetName().ToUpper();
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Row(TEXT("PRESET: ") + PresetName, [Act](int32 Step) { Act([Step](ADCPlayerController& P) { P.BuilderPreset(Step); })(); })];
			TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
			for (int32 i = 0; i < T.Run.CharacterSlots; ++i)
			{
				Slots->AddSlot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(120.f)[Button(FString::Printf(TEXT("SLOT %d"), i + 1),
					Act([i](ADCPlayerController& P) { P.BuilderSelectSlot(i); }), i == PC->GetBuilderSlot() ? Ink : Gold)]];
			}
			Slots->AddSlot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(150.f)[Button(TEXT("RANDOM"), Act([](ADCPlayerController& P) { P.BuilderRandom(); }))]];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[Slots];
			const FString Code = DCOutfit::Encode(O);
			TSharedRef<SEditableTextBox> CodeBox = SNew(SEditableTextBox).Text(FText::FromString(Code)).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Label(TEXT("SHARE CODE (PASTE A FRIEND'S HERE)"), 14, Ink)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(420.f).HeightOverride(44.f)[CodeBox]];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(206.f)[Button(TEXT("COPY CODE"), Act([](ADCPlayerController& P) { P.BuilderCopyCode(); }))]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(206.f)[Button(TEXT("LOAD CODE"), Act([CodeBox](ADCPlayerController& P) { P.BuilderImport(CodeBox->GetText().ToString()); }))]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(206.f)[Button(TEXT("ID CARD"), Act([](ADCPlayerController& P) { P.BuilderExportCard(); }))]]];
			if (!PC->GetBuilderMessage().IsEmpty())
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[Label(PC->GetBuilderMessage(), 14, Ink)];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Button(TEXT("SAVE AND BACK"), Click(&ADCPlayerController::CloseBuilder), Ink)];
		},
		[&]()
		{
			// Settings (R24): every change is saved at once (Saved/DCSave/settings.json).
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[Label(TEXT("SETTINGS"), 30, Ink)];
			for (const TPair<FName, const TCHAR*>& R : {TPair<FName, const TCHAR*>(TEXT("Volume"), TEXT("VOLUME")), TPair<FName, const TCHAR*>(TEXT("Mouse"), TEXT("MOUSE")),
				TPair<FName, const TCHAR*>(TEXT("Quality"), TEXT("QUALITY")), TPair<FName, const TCHAR*>(TEXT("Window"), TEXT("WINDOW")), TPair<FName, const TCHAR*>(TEXT("Hints"), TEXT("HINTS"))})
			{
				const FName Id = R.Key;
				auto Stepper = [Weak, Id](int32 D) { return [Weak, Id, D]() { if (ADCPlayerController* P = Weak.Get()) { P->SettingsStep(Id, D); } }; };
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
					[SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(52.f)[Button(TEXT("<"), Stepper(-1))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(300.f)[Label(FString::Printf(TEXT("%s: %s"), R.Value, *PC->SettingsValue(Id)), 20, Ink)]]
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(52.f)[Button(TEXT(">"), Stepper(1))]]];
			}
			// Keys: click a row, then press the new key (Esc cancels).
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 4)[Label(TEXT("KEYS (CLICK, THEN PRESS A KEY)"), 14, Ink)];
			TSharedRef<SUniformGridPanel> Keys = SNew(SUniformGridPanel).SlotPadding(FMargin(3.f));
			int32 Cell = 0;
			for (const FName& Action : UDCTuning::Get().Run.RebindableActions)
			{
				const bool bWaiting = PC->GetAwaitingKey() == Action;
				const FString Text = FString::Printf(TEXT("%s: %s"), *Action.ToString().ToUpper(), bWaiting ? TEXT("PRESS A KEY") : *PC->KeyFor(Action));
				Keys->AddSlot(Cell % 2, Cell / 2)[SNew(SBox).WidthOverride(320.f).HeightOverride(62.f)
					[Button(Text, [Weak, Action]() { if (ADCPlayerController* P = Weak.Get()) { P->BeginRebind(Action); } }, bWaiting ? Ink : Gold)]];
				++Cell;
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Keys];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[SNew(SBox).WidthOverride(260.f)[Button(TEXT("RESET KEYS"), Click(&ADCPlayerController::ResetKeys))]];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Button(TEXT("BACK"), Click(&ADCPlayerController::CloseSettings), Ink)];
		},
		[&]()
		{
			// R25 "How to play": sections from +HowToPlay (data), then PLAY (offered before the first run) or BACK.
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)[Label(TEXT("HOW TO PLAY"), 30, Ink)];
			for (const FDCHowToSection& Sec : UDCTuning::Get().HowToPlay)
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 10, 0, 2)[Label(Sec.Title, 18, Gold)];
				for (const FString& Line : Sec.Lines)
				{
					Column->AddSlot().AutoHeight().HAlign(HAlign_Left)[Label(Line, 14, Ink)];
				}
			}
			if (PC->IsHowToThenSolo())
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 16, 0, 0)[Button(TEXT("PLAY"), Click(&ADCPlayerController::HowToPlaySolo), Gold, &GSoloButton)];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, PC->IsHowToThenSolo() ? 4 : 16, 0, 0)[Button(TEXT("BACK"), Click(&ADCPlayerController::CloseHowTo), Ink)];
		}};
		Modes[FMath::Min<int32>(Mode, UE_ARRAY_COUNT(Modes) - 1)]();
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)
			[bBuilderPage ? SNullWidget::NullWidget : Label(FString::Printf(TEXT("WASD move  %s sprint  %s jump  %s crouch  |  1 rifle  2 strike  3 camera\n%s raise camera  %s shoot / call / snap  |  %s revive  [ ] sensitivity"), *PC->KeyFor(TEXT("Sprint")).ToUpper(), *PC->KeyFor(TEXT("Jump")).ToUpper(), *PC->KeyFor(TEXT("Crouch")).ToUpper(), *PC->KeyFor(TEXT("Aim")).ToUpper(), *PC->KeyFor(TEXT("Fire")).ToUpper(), *PC->KeyFor(TEXT("Interact")).ToUpper()), 12, Ink)]; // the saved bindings

		// Key art fills the screen (generated, R11); the menu sits on its empty left third, so the first ten seconds
		// show the game: a beam, a toppling voxel tower, a photographer (R7).
		static FSlateBrush KeyArt;
		if (!KeyArt.GetResourceObject())
		{
			if (UTexture2D* Art = Cast<UTexture2D>(UDCTuning::Get().Assets.MenuKeyArt.TryLoad()))
			{
				Art->AddToRoot();
				KeyArt.SetResourceObject(Art);
				KeyArt.ImageSize = FVector2D(Art->GetSizeX(), Art->GetSizeY());
				KeyArt.DrawAs = ESlateBrushDrawType::Image;
			}
		}
		TSharedRef<SOverlay> Root = SNew(SOverlay);
		Root->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Paper)];
		if (KeyArt.GetResourceObject())
		{
			Root->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SNew(SImage).Image(&KeyArt)]];
		}
		// The builder's turntable fills the right side on paper (it replaces the key art there).
		static FSlateBrush PreviewBrush;
		if (Mode == 3 && PC->GetBuilderImage())
		{
			PreviewBrush.SetResourceObject(PC->GetBuilderImage());
			PreviewBrush.ImageSize = FVector2D(512.f, 640.f);
			PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
			Root->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Paper)];
			Root->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(0, 0, 160, 0))
				[SNew(SBox).WidthOverride(640.f).HeightOverride(800.f)[SNew(SImage).Image(&PreviewBrush).ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([WeakPC = TWeakObjectPtr<ADCPlayerController>(PC)]()
					{
						// Paper shows through until the turntable has lit frames (no black box on open).
						return FSlateColor(FLinearColor(1.f, 1.f, 1.f, WeakPC.IsValid() ? WeakPC->GetBuilderImageAlpha() : 0.f));
					}))]];
		}
		Root->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(60, 0, 0, 0))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(Paper.R, Paper.G, Paper.B, 0.9f))
				.Padding(FMargin(40, 28))
				[Column]
			];
		return Root;
	}
}

TWeakPtr<SWidget> DCMenu::SoloButton()
{
	return GSoloButton;
}
