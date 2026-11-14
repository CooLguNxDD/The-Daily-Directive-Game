#include "DCMenu.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rules/DCOutfit.h"
#include "DCFonts.h"
#include "DCPalette.h"

#include "DCAudio.h"
#include "DCGameState.h"
#include "DCLanBeacon.h"
#include "DCPlayerController.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Input/SButton.h"
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

	/** Flat buttons: a white brush the fill colour tints exactly (the engine's grey button turned yellow into olive). */
	const FButtonStyle& FlatButton()
	{
		static FButtonStyle Style = []
		{
			FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			S.SetNormal(FSlateColorBrush(FLinearColor::White));
			S.SetHovered(FSlateColorBrush(FLinearColor(0.85f, 0.85f, 0.85f)));
			S.SetPressed(FSlateColorBrush(FLinearColor(0.7f, 0.7f, 0.7f)));
			S.SetNormalPadding(FMargin(0));
			S.SetPressedPadding(FMargin(0));
			return S;
		}();
		return Style;
	}

	TWeakPtr<SWidget> GSoloButton;

	TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> OnClick, const FLinearColor& Fill = Gold, TWeakPtr<SWidget>* OutButton = nullptr)
	{
		TSharedPtr<SButton> Made;
		TSharedRef<SWidget> Box = SNew(SBox).WidthOverride(420.f).HeightOverride(64.f).Padding(FMargin(0, 6))
			[
				SAssignNew(Made, SButton)
				.ButtonStyle(&FlatButton())
				.ButtonColorAndOpacity(Fill)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(22)).ColorAndOpacity(Fill.Equals(Ink) ? Paper : Ink)
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
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(TEXT("DIRECTOR'S CUT"), 64, Ink)];
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(TEXT("FRONT PAGE"), 40, Ink)];
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 24)
			[Label(TEXT("Call the strike. Run toward it. Snap the collapse at peak bloom.\nYour photos buy bigger bombs. The best one makes the front page."), 16, Ink)];

		const uint16 Port = static_cast<uint16>(UDCTuning::Get().Run.Port);
		// One builder per menu mode, picked by index.
		const TFunction<void()> Modes[] = {
		[&]()
		{
			TSharedRef<SEditableTextBox> Address = SNew(SEditableTextBox).Text(FText::FromString(TEXT("127.0.0.1"))).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("SOLO RUN"), Click(&ADCPlayerController::MenuSolo), Gold, &GSoloButton)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("HOST CO-OP (1-4)"), Click(&ADCPlayerController::MenuHost))];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("YOUR PHOTOGRAPHER"), Click(&ADCPlayerController::OpenBuilder))];
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
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(FString::Printf(TEXT("HOSTING. FRIENDS JOIN: %s (port %d)"), *LocalAddress(), Port), 20, Ink)];
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
			// Photographer builder (R23): one row per slot, two swatch rows, save slots, share code in and out.
			const UDCTuning& T = UDCTuning::Get();
			const FDCOutfit& O = PC->GetBuilderOutfit();
			auto Row = [&](const FString& Name, TFunction<void(int32)> Step)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(52.f)[Button(TEXT("<"), [Step]() { Step(-1); }, Gold)]]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(300.f)[Label(Name, 20, Ink)]]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70.f).HeightOverride(52.f)[Button(TEXT(">"), [Step]() { Step(1); }, Gold)]];
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
			TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
			for (int32 i = 0; i < T.Run.CharacterSlots; ++i)
			{
				Slots->AddSlot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(130.f)[Button(FString::Printf(TEXT("SLOT %d"), i + 1),
					Act([i](ADCPlayerController& P) { P.BuilderSelectSlot(i); }), i == PC->GetBuilderSlot() ? Ink : Gold)]];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[Slots];
			const FString Code = DCOutfit::Encode(O);
			TSharedRef<SEditableTextBox> CodeBox = SNew(SEditableTextBox).Text(FText::FromString(Code)).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Label(TEXT("SHARE CODE (PASTE A FRIEND'S HERE)"), 14, Ink)];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(420.f).HeightOverride(44.f)[CodeBox]];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(206.f)[Button(TEXT("COPY CODE"), Act([](ADCPlayerController& P) { P.BuilderCopyCode(); }))]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SBox).WidthOverride(206.f)[Button(TEXT("LOAD CODE"), Act([CodeBox](ADCPlayerController& P) { P.BuilderImport(CodeBox->GetText().ToString()); }))]]];
			if (!PC->GetBuilderMessage().IsEmpty())
			{
				Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[Label(PC->GetBuilderMessage(), 14, Ink)];
			}
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Button(TEXT("SAVE AND BACK"), Click(&ADCPlayerController::CloseBuilder), Ink)];
		}};
		Modes[FMath::Min<int32>(Mode, UE_ARRAY_COUNT(Modes) - 1)]();
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)
			[Label(TEXT("WASD move  SHIFT sprint  SPACE jump  CTRL crouch  |  1 rifle  2 strike  3 camera\nRMB raise camera  LMB shoot / call / snap  |  E revive  [ ] sensitivity"), 12, Ink)];

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
				[SNew(SBox).WidthOverride(640.f).HeightOverride(800.f)[SNew(SImage).Image(&PreviewBrush)]];
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
