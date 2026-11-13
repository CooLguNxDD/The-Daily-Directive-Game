#include "DCMenu.h"
#include "DCPalette.h"

#include "DCAudio.h"
#include "DCGameState.h"
#include "DCPlayerController.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

using namespace DCPalette;

namespace DCMenuStyle
{

	FSlateFontInfo Font(int32 Size, const char* Weight = "Bold")
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	TSharedRef<SWidget> Label(const FString& Text, int32 Size, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Color).Justification(ETextJustify::Center).AutoWrapText(true);
	}

	TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> OnClick, const FLinearColor& Fill = Red)
	{
		return SNew(SBox).WidthOverride(420.f).HeightOverride(64.f).Padding(FMargin(0, 6))
			[
				SNew(SButton)
				.ButtonColorAndOpacity(Fill)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(22)).ColorAndOpacity(Paper)
				]
			];
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

	TSharedRef<SWidget> Build(ADCPlayerController* PC, uint8 Mode)
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
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Label(TEXT("FRONT PAGE"), 40, Red)];
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 24)
			[Label(TEXT("Call the strike. Run toward it. Snap the collapse at peak bloom.\nYour photos buy bigger bombs. The best one makes the front page."), 16, Ink)];

		const uint16 Port = static_cast<uint16>(UDCTuning::Get().Run.Port);
		// One builder per menu mode, picked by index.
		const TFunction<void()> Modes[] = {
		[&]()
		{
			TSharedRef<SEditableTextBox> Address = SNew(SEditableTextBox).Text(FText::FromString(TEXT("127.0.0.1"))).Font(Font(20, "Regular")).Justification(ETextJustify::Center);
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("SOLO RUN"), Click(&ADCPlayerController::MenuSolo))];
			Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Button(TEXT("HOST CO-OP (1-4)"), Click(&ADCPlayerController::MenuHost))];
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
		}};
		Modes[FMath::Min<int32>(Mode, UE_ARRAY_COUNT(Modes) - 1)]();
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)
			[Label(TEXT("WASD move  SHIFT sprint  SPACE jump  CTRL crouch  |  1 rifle  2 strike  3 camera  |  RMB raise camera  LMB shoot / call / snap  |  E revive  [ ] sensitivity"), 12, Ink)];

		return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Paper)
				.Padding(FMargin(48, 32))
				[Column]
			];
	}
}
