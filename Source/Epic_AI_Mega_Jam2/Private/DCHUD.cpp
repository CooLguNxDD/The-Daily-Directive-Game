#include "DCHUD.h"
#include "DCFonts.h"
#include "CanvasItem.h"
#include "EngineFontServices.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"

#include "Engine/Texture2D.h"
#include "DCPalette.h"

#include "DCCharacter.h"
#include "DCEnvironment.h"
#include "DCGameState.h"
#include "DCPhotoProbe.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "DCVoxelCity.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

using namespace DCPalette;

namespace
{

	FString Upper(const FName& N) { return N.ToString().ToUpper(); }

	FString FormatPoints(int32 V)
	{
		return FText::AsNumber(V).ToString();
	}
}

void ADCHUD::BeginPlay()
{
	Super::BeginPlay();
	BigFont = DCFonts::Get(TEXT("Body"));
	HeadFont = DCFonts::Get(TEXT("Headline"));
	MastFont = DCFonts::Get(TEXT("Masthead"));
	StampFont = DCFonts::Get(TEXT("Stamp"));
	for (const FDCUpgradeDef& U : UDCTuning::Get().Upgrades)
	{
		if (UTexture2D* Tex = U.Icon.IsNull() ? nullptr : Cast<UTexture2D>(U.Icon.TryLoad()))
		{
			CardIcons.Add(U.Id, Tex);
		}
	}
	SmallFont = GEngine->GetMediumFont();
}

namespace
{
	// The font at the drawn size: glyphs are rasterised at it rather than stretched from the base size (crisp type).
	FSlateFontInfo SizedFont(const UFont* F, float PixelScale)
	{
		FSlateFontInfo Info = F->GetLegacySlateFontInfo();
		Info.Size = FMath::Max(1.f, Info.Size * PixelScale);
		return Info;
	}
}

void ADCHUD::TextSize(UFont* Font, const FString& S, float PixelScale, float& OutW, float& OutH) const
{
	UFont* F = Font ? Font : BigFont.Get();
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::IsInitialized() ? FEngineFontServices::Get().GetFontMeasure() : nullptr;
	if (!Measure.IsValid() || F->FontCacheType != EFontCacheType::Runtime)
	{
		Canvas->TextSize(F, S, OutW, OutH, PixelScale, PixelScale);
		return;
	}
	const FVector2D Size = Measure->Measure(S, SizedFont(F, PixelScale));
	OutW = Size.X;
	OutH = Size.Y;
}

void ADCHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale, bool bCentre, UFont* Font)
{
	UFont* F = Font ? Font : BigFont.Get();
	const float PixelScale = Scale * TextBoost * UiScale;
	float W = 0.f, H = 0.f;
	if (bCentre)
	{
		TextSize(F, S, PixelScale, W, H);
	}
	const bool bRuntime = F->FontCacheType == EFontCacheType::Runtime;
	auto Draw = [&](float Px, float Py, const FLinearColor& Color)
	{
		if (!bRuntime)
		{
			Canvas->SetDrawColor(Color.ToFColor(true));
			Canvas->DrawText(F, S, Px, Py, PixelScale, PixelScale);
			return;
		}
		FCanvasTextItem Item(FVector2D(Px, Py), FText::FromString(S), SizedFont(F, PixelScale), Color);
		Canvas->DrawItem(Item);
	};
	// Drop shadow keeps light text readable over a bloom flash; dark ink on paper needs none.
	if (C.GetLuminance() > 0.35f)
	{
		Draw(X - W * 0.5f + 2.f, Y + 2.f, FLinearColor(0.f, 0.f, 0.f, C.A * 0.78f));
	}
	Draw(X - W * 0.5f, Y, C);
}

void ADCHUD::Box(float X, float Y, float W, float H, const FLinearColor& C)
{
	DrawRect(C, X, Y, W, H);
}

void ADCHUD::Frame(float X, float Y, float W, float H, float T, const FLinearColor& C)
{
	Box(X, Y, W, T, C);
	Box(X, Y + H - T, W, T, C);
	Box(X, Y, T, H, C);
	Box(X + W - T, Y, T, H, C);
}

void ADCHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UiScale = Canvas->ClipY / 1080.f;
	RealNow = GetWorld()->GetRealTimeSeconds();
	ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	ADCCharacter* Me = Cast<ADCCharacter>(GetOwningPawn());
	if (!GS)
	{
		return;
	}
	using FDraw = void (ADCHUD::*)(ADCGameState&, ADCCharacter*);
	static const TMap<EDCRunPhase, FDraw> Phases = {
		{EDCRunPhase::Lobby, &ADCHUD::DrawLobby},
		{EDCRunPhase::Sector, &ADCHUD::DrawSector},
		{EDCRunPhase::Draft, &ADCHUD::DrawDraft},
		{EDCRunPhase::Reel, &ADCHUD::DrawReel},
		{EDCRunPhase::FinalCut, &ADCHUD::DrawFinalCut},
		{EDCRunPhase::Vote, &ADCHUD::DrawVote},
		{EDCRunPhase::FrontPage, &ADCHUD::DrawFrontPage},
	};
	if (const FDraw* Draw = Phases.Find(GS->Phase))
	{
		(this->**Draw)(*GS, Me);
	}
}

void ADCHUD::DrawSector(ADCGameState& GS, ADCCharacter* Me)
{
	if (!Me)
	{
		return;
	}
	TrackOnboarding(*Me);
	DrawFlashes(GS, *Me);
	if (Me->IsCameraRaised() && Me->GetSlot() == EDCSlot::Camera) // never over another slot while the raise flag catches up
	{
		DrawViewfinder(*Me);
	}
	else
	{
		DrawCrosshair(*Me);
		DrawStrikeInfo(*Me);
	}
	DrawStatus(GS, *Me);
	DrawScoop(GS);
	DrawHurt(*Me);
	if (ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner))
	{
		DrawStamp(*PC);
	}
	DrawOnboarding(*Me);
	DrawDowned(*Me);
	if (GS.Phase == EDCRunPhase::Sector || GS.Phase == EDCRunPhase::Lobby || GS.Phase == EDCRunPhase::Draft)
	{
		DrawToast(*Me); // the last sector's toast never rides into the reel (packaged frame 90)
	}
	DrawSectorCard(GS);
}

void ADCHUD::DrawSectorCard(ADCGameState& GS)
{
	const UDCTuning& T = UDCTuning::Get();
	if (CardSector != GS.SectorIndex)
	{
		CardSector = GS.SectorIndex;
		CardLoadedReal = -1.0;
	}
	const ADCVoxelCity* City = ADCVoxelCity::IsEnabled() ? ADCVoxelCity::Get(this) : nullptr;
	const bool bLoaded = !City || (City->IsLoaded() && City->GetBuiltSector() == GS.SectorIndex);
	if (bLoaded && CardLoadedReal < 0.0)
	{
		CardLoadedReal = RealNow;
	}
	// Hold while loading and a beat after, then lift away over SectorCardFadeSeconds.
	const float Since = CardLoadedReal < 0.0 ? 0.f : static_cast<float>(RealNow - CardLoadedReal);
	const float A = 1.f - FMath::Clamp((Since - T.Look.SectorCardHoldSeconds) / FMath::Max(T.Look.SectorCardFadeSeconds, 0.01f), 0.f, 1.f);
	if (A <= 0.f || GS.Now() - GS.SectorStartTime > T.Look.SectorCardMaxSeconds)
	{
		return;
	}
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const FDCSectorDef* Sector = T.Sectors.IsValidIndex(GS.SectorIndex) ? &T.Sectors[GS.SectorIndex] : nullptr;
	const FString Name = Sector ? Sector->Name.ToString().ToUpper() : FString::Printf(TEXT("SECTOR %d"), GS.SectorIndex + 1);
	const float Lift = (1.f - A) * H * 0.15f;
	// The dark glass fades with the card (V9.6: it used to stay opaque through the fade); type and rules come from
	// Look data, light on the glass, and lift away with it.
	PaperSheet(0, -Lift, W, H, A);
	const FLinearColor& TitleC = T.Look.SectorCardTitle;
	const FLinearColor& RuleC = T.Look.SectorCardRule;
	const FLinearColor TitleA(TitleC.R, TitleC.G, TitleC.B, TitleC.A * A);
	const FLinearColor RuleA(RuleC.R, RuleC.G, RuleC.B, RuleC.A * A);
	// A dark-glass plate in the middle with thin neon edges (critic V20 minor: the card read as an empty navy screen).
	Box(W * 0.17f, H * 0.33f - Lift, W * 0.66f, H * 0.38f, FLinearColor(0.02f, 0.03f, 0.06f, 0.85f * A));
	Box(W * 0.17f, H * 0.33f - Lift, W * 0.66f, Sx(2.f), FLinearColor(Magenta.R, Magenta.G, Magenta.B, 0.9f * A));
	Box(W * 0.17f, H * 0.71f - Lift - Sx(2.f), W * 0.66f, Sx(2.f), FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.9f * A));
	Box(W * 0.17f, H * 0.33f - Lift, Sx(4.f), H * 0.38f, FLinearColor(Magenta.R, Magenta.G, Magenta.B, 0.9f * A));
	Box(W * 0.2f, H * 0.36f - Lift, W * 0.6f, Sx(3.f), RuleA);
	Text(FString::Printf(TEXT("SECTOR %d OF %d"), GS.SectorIndex + 1, FMath::Max(T.Sectors.Num(), 1)), W * 0.5f, H * 0.385f - Lift, RuleA, 1.1f, true);
	Text(Name, W * 0.5f, H * 0.44f - Lift, TitleA, T.Look.SectorCardTitleScale, true, HeadFont);
	Box(W * 0.2f, H * 0.62f - Lift, W * 0.6f, Sx(3.f), RuleA);
	Text(TEXT("CAMERAS READY. THE CITY IS YOUR STUDIO."), W * 0.5f, H * 0.645f - Lift, TitleA, 0.95f, true);
}

void ADCHUD::DrawFlashes(ADCGameState& GS, ADCCharacter& Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if (const ADCEnvironment* Env = GS.GetEnvironment())
	{
		const float A = Env->GetFlashAlpha();
		if (A > 0.01f)
		{
			const FLinearColor& Tint = UDCTuning::Get().Look.FlashOverlay; // a warm tint at the peak, not a white sheet
			Box(0, 0, W, H, FLinearColor(Tint.R, Tint.G, Tint.B, Tint.A * A));
		}
	}
	const float Shutter = static_cast<float>(RealNow - Me.Feedback().ShutterTime);
	// The shutter blink (data): a short dark blink, the lens closing; a white sheet here made PEAK CINEMA a white frame.
	const FDCLookTuning& FlashLook = UDCTuning::Get().Look;
	if (Shutter < FlashLook.ShutterBlinkSeconds)
	{
		const FLinearColor& Blink = FlashLook.ShutterBlink;
		Box(0, 0, W, H, FLinearColor(Blink.R, Blink.G, Blink.B, Blink.A * (1.f - Shutter / FMath::Max(FlashLook.ShutterBlinkSeconds, 0.01f))));
	}
	if (Me.IsSpotlit())
	{
		const FLinearColor& Veil = FlashLook.SpotlitVeil; // data (was a 0.35-0.45 white fill: the grey veil)
		Box(0, 0, W, H, FLinearColor(Veil.R, Veil.G, Veil.B, FMath::Max(0.f, Veil.A + FlashLook.SpotlitVeilPulse * FMath::Sin(RealNow * UE_TWO_PI * FlashLook.SpotlitVeilHz))));
		Text(TEXT("SPOTLIGHT! EXPOSURE BLOWN - SHOOT THE LIGHT"), W * 0.5f, H * 0.3f, Red, 1.1f, true);
	}
}

void ADCHUD::DrawCrosshair(ADCCharacter& Me)
{
	const float Cx = Canvas->ClipX * 0.5f, Cy = Canvas->ClipY * 0.5f;
	const FLinearColor C(1, 1, 1, 0.9f);
	const float Gap = Sx(Me.GetSlot() == EDCSlot::Rifle ? 8.f : 4.f), Len = Sx(10.f);
	DrawLine(Cx - Gap - Len, Cy, Cx - Gap, Cy, C, 2.f);
	DrawLine(Cx + Gap, Cy, Cx + Gap + Len, Cy, C, 2.f);
	DrawLine(Cx, Cy - Gap - Len, Cx, Cy - Gap, C, 2.f);
	DrawLine(Cx, Cy + Gap, Cx, Cy + Gap + Len, C, 2.f);

	const float Hit = static_cast<float>(RealNow - Me.Feedback().HitMarkerTime);
	if (Hit < 0.2f)
	{
		const FLinearColor HC = Me.Feedback().bLastHitKill ? Red : FLinearColor::White;
		const float D = Sx(6.f), L = Sx(Me.Feedback().bLastHitKill ? 14.f : 10.f);
		DrawLine(Cx - D - L, Cy - D - L, Cx - D, Cy - D, HC, 3.f);
		DrawLine(Cx + D + L, Cy - D - L, Cx + D, Cy - D, HC, 3.f);
		DrawLine(Cx - D - L, Cy + D + L, Cx - D, Cy + D, HC, 3.f);
		DrawLine(Cx + D + L, Cy + D + L, Cx + D, Cy + D, HC, 3.f);
	}
}

void ADCHUD::DrawStrikeInfo(ADCCharacter& Me)
{
	if (Me.GetSlot() != EDCSlot::Stratagem)
	{
		return;
	}
	const ADCPlayerState* PS = Me.GetPlayerState<ADCPlayerState>();
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!PS || !GS)
	{
		return;
	}
	const float X = Canvas->ClipX * 0.5f + Sx(40.f);
	float Y = Canvas->ClipY * 0.5f - Sx(60.f);
	// A paper plate with ink type (it sat as thin white text on the bright street); the live pick on a yellow bar.
	Box(X - Sx(14.f), Y - Sx(10.f), Sx(470.f), Sx(30.f) + PS->OwnedStratagems.Num() * Sx(26.f) + Sx(44.f), Paper);
	Box(X - Sx(14.f), Y - Sx(10.f), Sx(5.f), Sx(30.f) + PS->OwnedStratagems.Num() * Sx(26.f) + Sx(44.f), Ink);
	Text(TEXT("ORBITAL CALL-IN"), X, Y, Ink, 0.8f, false, HeadFont);
	Y += Sx(30.f);
	for (const FName& Id : PS->OwnedStratagems)
	{
		const FDCStratagemDef* Def = UDCTuning::Get().FindStratagem(Id);
		if (!Def)
		{
			continue;
		}
		const float Ready = PS->CooldownReadyTime(Id) - GS->Now();
		const bool bSel = Id == Me.GetSelectedStratagem();
		const bool bAfford = PS->Points >= Def->Cost;
		FString Line = FString::Printf(TEXT("%s %s  %s"), bSel ? TEXT(">") : TEXT(" "), *Def->DisplayName.ToString().ToUpper(), Def->Cost > 0 ? *FString::Printf(TEXT("%d PP"), Def->Cost) : TEXT("FREE"));
		Line += Ready > 0.f ? FString::Printf(TEXT("  [%.0fs]"), Ready) : TEXT("");
		if (bSel)
		{
			Box(X - Sx(6.f), Y - Sx(1.f), Sx(450.f), Sx(25.f), bAfford && Ready <= 0.f ? Gold : FLinearColor(Red.R, Red.G, Red.B, 0.25f));
		}
		Text(Line, X, Y, bSel ? (bAfford && Ready <= 0.f ? Night : Ink) : FLinearColor(Ink.R, Ink.G, Ink.B, 0.6f), 0.65f);
		Y += Sx(26.f);
	}
	Text(TEXT("LMB CALL   Q / WHEEL SWITCH"), X, Y + Sx(6.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.7f), 0.55f);
}

void ADCHUD::DrawViewfinder(ADCCharacter& Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	// Cyan HUD framing (critic V20 minor: the viewfinder reads as the same neon HUD as the feed and the stamp).
	const FLinearColor Line(Cyan.R, Cyan.G, Cyan.B, 0.85f);
	// Letterbox to the photo's 16:9 and corner brackets.
	const float FrameH = W * 9.f / 16.f;
	const float Bar = FMath::Max(0.f, (H - FrameH) * 0.5f);
	Box(0, 0, W, Bar, FLinearColor::Black);
	Box(0, H - Bar, W, Bar, FLinearColor::Black);
	// Brackets sit inside the status widgets (points, film top; health, ammo bottom), not over them.
	const float M = Sx(60.f), MY = Sx(120.f), L = Sx(70.f), T = 4.f;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const float X = (Corner % 2) ? W - M : M;
		const float Y = (Corner / 2) ? H - Bar - MY : Bar + MY;
		const float Dx = (Corner % 2) ? -L : L;
		const float Dy = (Corner / 2) ? -L : L;
		DrawLine(X, Y, X + Dx, Y, Line, T);
		DrawLine(X, Y, X, Y + Dy, Line, T);
	}
	// Thirds and the centre mark.
	const FLinearColor Faint(Cyan.R, Cyan.G, Cyan.B, 0.16f);
	DrawLine(W / 3, Bar, W / 3, H - Bar, Faint, 1.f);
	DrawLine(2 * W / 3, Bar, 2 * W / 3, H - Bar, Faint, 1.f);
	DrawLine(0, Bar + FrameH / 3, W, Bar + FrameH / 3, Faint, 1.f);
	DrawLine(0, Bar + 2 * FrameH / 3, W, Bar + 2 * FrameH / 3, Faint, 1.f);
	Frame(W * 0.5f - Sx(14.f), H * 0.5f - Sx(14.f), Sx(28.f), Sx(28.f), 2.f, Line);
	Box(W * 0.5f - 2.f, H * 0.5f - 2.f, 4.f, 4.f, Gold);

	// Live floating tags on things worth shooting (R2).
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	TArray<FDCLiveTag> LiveTags;
	DCPhotoProbe::CollectTags(GetWorld(), Me.GetShotView(), GS ? GS->Now() : 0.f, &Me, LiveTags);
	for (const FDCLiveTag& Tag : LiveTags)
	{
		const FVector S = Project(Tag.World, false);
		if (S.Z <= 0.f)
		{
			continue;
		}
		const float Pulse = 0.75f + 0.25f * FMath::Sin(RealNow * (6.f + 10.f * Tag.Heat));
		const FString Label = Upper(Tag.Tag);
		float Tw = 0.f, Th = 0.f;
		TextSize(BigFont, Label, 0.7f * TextBoost * UiScale, Tw, Th);
		// Target tags are things the player can use: signal yellow with ink type (red stays for danger, R6).
		// Never under the timer and quota bar at the top.
		const float Ty = FMath::Max(S.Y, Sx(120.f) + Th + 4.f);
		Box(S.X - Tw * 0.5f - 6.f, Ty - Th - 4.f, Tw + 12.f, Th + 4.f, FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f * Pulse));
		Text(Label, S.X, Ty - Th - 3.f, Night, 0.7f, true);
		DrawLine(S.X, Ty, S.X, Ty + Sx(18.f), Gold, 2.f);
	}

	// Film and wind-on.
	const ADCPlayerState* PS = Me.GetPlayerState<ADCPlayerState>();
	const int32 Shots = PS ? PS->ShotsLeft : 0;
	Text(FString::Printf(TEXT("FILM %d"), Shots), W - M - Sx(150.f), H - Bar - M - Sx(50.f), Shots > 3 ? FLinearColor::White : Red, 1.f);
	const float Zoom = UDCTuning::Get().Move.ThirdPersonFOV / FMath::Max(Me.GetZoomFOV(), 1.f);
	// Above the health block (they overlapped at 720p).
	Text(FString::Printf(TEXT("%.1fx  WHEEL ZOOM"), Zoom), M + Sx(10.f), H - Bar - M - Sx(115.f), FLinearColor::White, 0.7f);
	const float Wind = Me.GetWindOnLeft();
	if (Wind > 0.f)
	{
		const float U = 1.f - Wind / FMath::Max(UDCTuning::Get().Camera.WindOnSeconds, 0.01f);
		Box(W * 0.5f - Sx(100.f), H - Bar - M - Sx(20.f), Sx(200.f) * U, Sx(6.f), FLinearColor::White);
		Text(TEXT("WINDING"), W * 0.5f, H - Bar - M - Sx(50.f), FLinearColor(1, 1, 1, 0.7f), 0.6f, true);
	}
	else
	{
		// Bottom centre, where WINDING shows between shots: clear of the timer and the quota bar at the top.
		Text(TEXT("LMB  SNAP"), W * 0.5f, H - Bar - M - Sx(50.f), FLinearColor(1, 1, 1, 0.7f), 0.6f, true);
	}
}

void ADCHUD::DrawStatus(ADCGameState& GS, ADCCharacter& Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const ADCPlayerState* PS = Me.GetPlayerState<ADCPlayerState>();
	const UDCTuning& T = UDCTuning::Get();

	// Paper plates with ink type and a thin ink rule (art bible: cream and ink; Saltline HUD language): readable over
	// a bright sky and a blast alike.
	const FLinearColor Plate(Paper.R, Paper.G, Paper.B, 0.86f); // dark glass (R25): light type stays readable over the city
	// Propaganda Points, the currency the shots buy firepower with.
	Box(Sx(24.f), Sx(24.f), Sx(330.f), Sx(74.f), Plate);
	Box(Sx(24.f), Sx(24.f), Sx(5.f), Sx(74.f), Ink);
	Text(TEXT("PROPAGANDA POINTS"), Sx(40.f), Sx(28.f), Ink, 0.55f);
	Text(FormatPoints(PS ? PS->Points : 0), Sx(40.f), Sx(48.f), Ink, 1.4f);

	// Sector and timer.
	const float Left = FMath::Max(0.f, GS.SectorEndTime - GS.Now());
	const FDCSectorDef* Sector = T.Sectors.IsValidIndex(GS.SectorIndex) ? &T.Sectors[GS.SectorIndex] : nullptr;
	const FString Name = Sector ? Sector->Name.ToString().ToUpper() : FString::Printf(TEXT("SECTOR %d"), GS.SectorIndex + 1);
	Box(W * 0.5f - Sx(200.f), Sx(14.f), Sx(400.f), Sx(118.f), Plate);
	Text(FString::Printf(TEXT("SECTOR %d/%d  %s"), GS.SectorIndex + 1, FMath::Max(T.Sectors.Num(), 1), *Name), W * 0.5f, Sx(20.f), Ink, 0.7f, true);
	Text(FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Left) / 60, FMath::FloorToInt(Left) % 60), W * 0.5f, Sx(46.f), Left < 30.f ? Red : Ink, 1.3f, true);

	// HQ quota (R6): the crew's photo score against this sector's target.
	if (Sector && Sector->QuotaPoints > 0)
	{
		const float Q = FMath::Clamp(static_cast<float>(GS.SectorPhotoPoints) / Sector->QuotaPoints, 0.f, 1.f);
		const float Bw = Sx(360.f), Bx = W * 0.5f - Bw * 0.5f, By = Sx(96.f);
		Box(Bx, By, Bw, Sx(14.f), Ink);
		Box(Bx + Sx(2.f), By + Sx(2.f), (Bw - Sx(4.f)) * Q, Sx(10.f), Q >= 1.f ? Gold : Paper);
		Text(FString::Printf(TEXT("%s QUOTA %s / %s"), GS.PlayerArray.Num() > 1 ? TEXT("TEAM") : TEXT("HQ"), *FormatPoints(GS.SectorPhotoPoints), *FormatPoints(Sector->QuotaPoints)), W * 0.5f, By + Sx(14.f), Ink, 0.5f, true);
	}

	// Film counter is always visible (R7).
	const int32 Shots = PS ? PS->ShotsLeft : 0;
	const int32 Film = PS ? PS->FilmPerSector : 0;
	Box(W - Sx(250.f), Sx(24.f), Sx(226.f), Sx(74.f), Plate);
	Box(W - Sx(250.f), Sx(24.f), Sx(5.f), Sx(74.f), Ink);
	Text(TEXT("FILM"), W - Sx(234.f), Sx(28.f), Ink, 0.55f);
	Text(FString::Printf(TEXT("%d / %d"), Shots, Film), W - Sx(234.f), Sx(48.f), Shots > 3 ? Ink : Red, 1.4f);

	// Health.
	const float Hp = Me.GetHealth() / FMath::Max(T.Move.MaxHealth, 1.f);
	Box(Sx(24.f), H - Sx(92.f), Sx(300.f), Sx(54.f), Plate);
	Box(Sx(30.f), H - Sx(60.f), Sx(288.f), Sx(16.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.25f));
	Box(Sx(30.f), H - Sx(60.f), Sx(288.f) * Hp, Sx(16.f), Hp > 0.3f ? Ink : Red);
	Text(TEXT("HEALTH"), Sx(30.f), H - Sx(88.f), Ink, 0.55f);

	// Slot bar: 1 rifle, 2 strike, 3 camera.
	static const TCHAR* Names[] = {TEXT("1 RIFLE"), TEXT("2 STRIKE"), TEXT("3 CAMERA")};
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bActive = static_cast<int32>(Me.GetSlot()) == i;
		const float X = W * 0.5f - Sx(255.f) + i * Sx(170.f);
		Box(X, H - Sx(70.f), Sx(160.f), Sx(44.f), bActive ? FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f) : Plate);
		Text(Names[i], X + Sx(80.f), H - Sx(62.f), bActive ? Night : FLinearColor(Ink.R, Ink.G, Ink.B, 0.6f), 0.75f, true);
	}
	if (Me.IsDrawing())
	{
		Text(TEXT("DRAWING..."), W * 0.5f, H - Sx(100.f), FLinearColor(1, 1, 1, 0.7f), 0.6f, true);
	}

	// Ammo.
	if (Me.GetSlot() == EDCSlot::Rifle)
	{
		const int32 Ammo = Me.GetAmmo();
		const FLinearColor AC = Ammo > T.Rifle.LowAmmoWarning ? Ink : Red;
		Box(W - Sx(214.f), H - Sx(104.f), Sx(190.f), Sx(80.f), Paper);
		Box(W - Sx(214.f), H - Sx(104.f), Sx(5.f), Sx(80.f), Ink);
		Text(FString::Printf(TEXT("%d"), Ammo), W - Sx(200.f), H - Sx(100.f), AC, 2.f);
		Text(FString::Printf(TEXT("/ %d"), Me.GetReserve()), W - Sx(110.f), H - Sx(70.f), Ink, 0.8f);
		if (Me.IsReloading())
		{
			Text(TEXT("RELOADING"), W * 0.5f, H * 0.5f + Sx(40.f), FLinearColor::White, 0.7f, true);
		}
		else if (Ammo == 0)
		{
			Text(Me.GetReserve() > 0 ? TEXT("R  RELOAD") : TEXT("OUT OF AMMO - USE STRIKES"), W * 0.5f, H * 0.5f + Sx(40.f), Red, 0.7f, true);
		}
	}
}

FString ADCHUD::FeedStamp(int32 PhotoId, float ShotTime)
{
	const int32 Centis = FMath::Max(0, FMath::RoundToInt32(ShotTime * 100.f));
	return FString::Printf(TEXT("LIVE  FEED %04d  T+%02d:%02d.%02d"), PhotoId % 10000, Centis / 6000, (Centis / 100) % 60, Centis % 100);
}

void ADCHUD::DrawPhotoCard(UTexture* Image, const FDCPhotoGrade& Grade, float X, float Y, float W, float StampSlam, bool bShowStamp, const FString& Feed)
{
	const float H = W * 9.f / 16.f;
	const float Border = Sx(10.f);
	Box(X - Border, Y - Border, W + Border * 2, H + Border * 2 + Sx(30.f), Paper);
	if (Image)
	{
		DrawTexture(Image, X, Y, W, H, 0, 0, 1, 1, FLinearColor::White, BLEND_Opaque);
	}
	else
	{
		Box(X, Y, W, H, FLinearColor(0.15f, 0.14f, 0.13f));
		Text(TEXT("DEVELOPING..."), X + W * 0.5f, Y + H * 0.45f, Ink, 0.7f, true);
	}
	if (!Feed.IsEmpty())
	{
		// Feed timestamp (R25 news feed): a cyan live dot and the clock on a dark strip at the print's lower left.
		const float FeedScale = FMath::Clamp(W / Sx(640.f), 0.4f, 1.f) * 0.5f;
		float Fw = 0.f, Fh = 0.f;
		TextSize(SmallFont, Feed, FeedScale * 1.6f * TextBoost * UiScale, Fw, Fh);
		const float Fx = X + Sx(14.f), Fy = Y + H - Fh - Sx(14.f);
		Box(Fx - Sx(4.f), Fy - Sx(2.f), Fw + Fh + Sx(14.f), Fh + Sx(4.f), FLinearColor(Night.R, Night.G, Night.B, 0.7f));
		const float Blink = FMath::Fmod(static_cast<float>(RealNow), 1.f) < 0.6f ? 1.f : 0.35f;
		Box(Fx, Fy + Fh * 0.25f, Fh * 0.5f, Fh * 0.5f, FLinearColor(Cyan.R, Cyan.G, Cyan.B, Blink));
		Text(Feed, Fx + Fh * 0.8f, Fy, Cyan, FeedScale * 1.6f, false, SmallFont);
	}
	if (!bShowStamp)
	{
		return;
	}
	// The stamp slams in big and settles, tilted, in signal-yellow ink with worn grit (R5, R6: score stamps are yellow).
	const float S = FMath::Lerp(2.4f, 1.f, FMath::Clamp(StampSlam, 0.f, 1.f));
	const float Cx = X + W * 0.62f, Cy = Y + H * 0.62f;
	const FString Score = FString::Printf(TEXT("+%s"), *FormatPoints(Grade.Score));
	// %g keeps 0.75x exact (a fixed one-decimal format showed a 0.75x blow-out as 0.8x).
	const FString Timing = FString::Printf(TEXT("%s  %gx"), *Upper(Grade.TimingLabel), FMath::RoundToFloat(Grade.Multiplier * 100.f) / 100.f);
	float Tw = 0.f, Th = 0.f;
	TextSize(StampFont, Score, 1.6f * S * TextBoost * UiScale, Tw, Th);
	float TimingW = 0.f, TimingH = 0.f;
	TextSize(StampFont, Timing, 0.8f * S * TextBoost * UiScale, TimingW, TimingH);
	const float Bw = FMath::Max(Tw, TimingW) + Sx(30.f), Bh = Th * 1.9f + Sx(14.f); // fits both lines (review CP2)
	const float Bx = Cx - Bw * 0.5f, By = Cy - Sx(10.f);
	// Tilt: a per-photo angle from the score, so the same photo always lands the same way.
	const float Tilt = -7.f + static_cast<float>(Grade.Score % 5);
	const FVector Pivot(Cx, By + Bh * 0.5f, 0.f);
	Canvas->Canvas->PushRelativeTransform(FTranslationMatrix(-Pivot) * FRotationMatrix(FRotator(0.f, Tilt, 0.f)) * FTranslationMatrix(Pivot));
	// Holographic stamp (R25, V9.1: no opaque yellow plate): dark glass lit lime, a glowing lime double rule with a
	// soft halo, lime type with a slight magenta/cyan split, and faint scan bands across it.
	const FLinearColor InkStamp(Gold.R, Gold.G, Gold.B, 1.f);
	for (int32 k = 3; k >= 1; --k)
	{
		Frame(Bx - k * Sx(5.f), By - k * Sx(5.f), Bw + 2.f * k * Sx(5.f), Bh + 2.f * k * Sx(5.f), Sx(5.f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.09f * (4 - k)));
	}
	Box(Bx, By, Bw, Bh, FLinearColor(Night.R, Night.G, Night.B, 0.72f));
	Box(Bx, By, Bw, Bh, FLinearColor(Gold.R, Gold.G, Gold.B, 0.16f));
	for (float Sy = By + Sx(3.f); Sy < By + Bh; Sy += Sx(5.f))
	{
		Box(Bx, Sy, Bw, Sx(1.f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.08f));
	}
	Frame(Bx, By, Bw, Bh, Sx(4.f), InkStamp);
	Frame(Bx + Sx(7.f), By + Sx(7.f), Bw - Sx(14.f), Bh - Sx(14.f), Sx(1.5f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.7f));
	if (UTexture2D* Worn = Bh > Sx(70.f) ? StampGrit() : nullptr) // small stamps skip the grit: it aliases into noise
	{
		// Paper-coloured speckle over the frame ink (under the type, so small stamps stay legible): pressed by hand.
		// The generated mask is one whole worn stamp (edges and speckle) laid over the plate; the procedural one tiles.
		const bool bWhole = Worn->GetSizeX() == Worn->GetSizeY();
		DrawTexture(Worn, Bx, By, Bw, Bh, 0, 0, bWhole ? 1.f : Bw / Sx(256.f), bWhole ? 1.f : Bh / Sx(128.f), FLinearColor(Paper.R, Paper.G, Paper.B, bWhole ? 0.8f : 0.35f), BLEND_Translucent);
	}
	const float Split = Sx(2.f);
	Text(Score, Cx - Split, By + Sx(4.f), FLinearColor(Magenta.R, Magenta.G, Magenta.B, 0.55f), 1.6f * S, true, StampFont);
	Text(Score, Cx + Split, By + Sx(4.f), FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.55f), 1.6f * S, true, StampFont);
	Text(Score, Cx, By + Sx(4.f), InkStamp, 1.6f * S, true, StampFont);
	Text(Timing, Cx, By + Sx(4.f) + Th * 1.0f, InkStamp, 0.8f * S, true, StampFont);
	Canvas->Canvas->PopTransform();
	float Ty = Y + Sx(16.f);
	// Category words as ink type on signal-yellow tags, inset inside the print's border (a blown-out spotlit shot keeps
	// a red tag: that one is a warning).
	const float TagScale = FMath::Clamp(W / Sx(640.f), 0.45f, 1.f);
	for (const FName& Category : Grade.Categories)
	{
		const FString Word = Upper(Category);
		float Cw = 0.f, Ch = 0.f;
		TextSize(BigFont, Word, 0.8f * TagScale * TextBoost * UiScale, Cw, Ch);
		const bool bWarn = Grade.bSpotlit && Category == Grade.Categories[0];
		Box(X + Sx(14.f), Ty, Cw + Sx(12.f), Ch + Sx(2.f), bWarn ? Red : Gold);
		Text(Word, X + Sx(20.f), Ty, Night, 0.8f * TagScale);
		Ty += Ch + Sx(6.f);
	}
}

void ADCHUD::PaperSheet(float X, float Y, float W, float H, float Alpha)
{
	if (!bPaperTried)
	{
		bPaperTried = true;
		PaperTex = Cast<UTexture2D>(UDCTuning::Get().Assets.PaperGrain.TryLoad());
	}
	// R25: a dark glass sheet (no paper left): night ground, faint scanlines, thin cyan rules at the top and bottom.
	Box(X, Y, W, H, FLinearColor(Paper.R, Paper.G, Paper.B, Alpha));
	for (float Ly = Y; Ly < Y + H; Ly += Sx(4.f))
	{
		Box(X, Ly, W, 1.f, FLinearColor(Ink.R, Ink.G, Ink.B, 0.025f * Alpha));
	}
	Box(X, Y + Sx(4.f), W, Sx(2.f), FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.8f * Alpha));
	Box(X, Y + H - Sx(6.f), W, Sx(2.f), FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.8f * Alpha));
}

UTexture2D* ADCHUD::StampGrit()
{
	if (Grit)
	{
		return Grit;
	}
	// The generated worn-ink mask (R11) when it shipped; else speckles and worn streaks made here (seeded).
	if (UTexture2D* Made = Cast<UTexture2D>(UDCTuning::Get().Assets.StampGrit.TryLoad()))
	{
		Grit = Made;
		return Grit;
	}
	constexpr int32 Gw = 256, Gh = 128;
	TArray<FColor> Px;
	Px.SetNumZeroed(Gw * Gh);
	FRandomStream Rng(0x57A3);
	for (int32 i = 0; i < 900; ++i)
	{
		const int32 X0 = Rng.RandRange(0, Gw - 1), Y0 = Rng.RandRange(0, Gh - 1);
		const int32 R = Rng.RandRange(0, 2);
		for (int32 Dy = -R; Dy <= R; ++Dy)
		{
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				Px[((Y0 + Dy + Gh) % Gh) * Gw + (X0 + Dx + Gw) % Gw] = FColor(255, 255, 255, static_cast<uint8>(Rng.RandRange(150, 255)));
			}
		}
	}
	for (int32 i = 0; i < 14; ++i)
	{
		const int32 Y0 = Rng.RandRange(0, Gh - 1), X0 = Rng.RandRange(0, Gw - 1), Len = Rng.RandRange(20, 90);
		for (int32 k = 0; k < Len; ++k)
		{
			Px[((Y0 + k / 12) % Gh) * Gw + (X0 + k) % Gw] = FColor(255, 255, 255, 200);
		}
	}
	Grit = UTexture2D::CreateTransient(Gw, Gh, PF_B8G8R8A8);
	if (Grit)
	{
		void* Mip = Grit->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip, Px.GetData(), Px.Num() * sizeof(FColor));
		Grit->GetPlatformData()->Mips[0].BulkData.Unlock();
		Grit->AddressX = TA_Wrap;
		Grit->AddressY = TA_Wrap;
		Grit->UpdateResource();
	}
	return Grit;
}

void ADCHUD::DrawStamp(ADCPlayerController& PC)
{
	const FDCLocalPhoto* Photo = PC.GetLastPhoto();
	const float Hold = UDCTuning::Get().Camera.StampHoldSeconds;
	if (!Photo)
	{
		return;
	}
	const float Age = static_cast<float>(RealNow - Photo->TakenReal);
	if (Age > Hold)
	{
		return;
	}
	const float W = Sx(520.f);
	// Slides a short way in and stays whole on screen (a card half off the edge read as cut off: critic CP26).
	const float SlideIn = FMath::Clamp(Age / 0.12f, 0.f, 1.f);
	const float X = Canvas->ClipX - (W + Sx(40.f)) + Sx(60.f) * (1.f - SlideIn);
	DrawPhotoCard(Photo->Image, Photo->Grade, X, Sx(130.f), W, Age / 0.15f, true, FeedStamp(Photo->PhotoId, Photo->ShotTime));
}

void ADCHUD::DrawHurt(ADCCharacter& Me)
{
	if (Me.IsDowned())
	{
		return; // downed: the red edge frame says it; hits on the ground must not wash the Last Frame view red
	}
	const float Age = static_cast<float>(RealNow - Me.Feedback().HurtTime);
	if (Age > 0.8f)
	{
		return;
	}
	const FVector To = Me.Feedback().HurtFrom - Me.GetActorLocation();
	const float Angle = FMath::DegreesToRadians(To.Rotation().Yaw - Me.GetControlRotation().Yaw);
	const float Cx = Canvas->ClipX * 0.5f, Cy = Canvas->ClipY * 0.5f;
	const float R = Canvas->ClipY * 0.38f;
	const FLinearColor C(Red.R, Red.G, Red.B, 0.85f * (1.f - Age / 0.8f));
	for (int32 i = -3; i <= 3; ++i)
	{
		const float A = Angle + i * 0.07f;
		const float X = Cx + FMath::Sin(A) * R;
		const float Y = Cy - FMath::Cos(A) * R;
		Box(X - Sx(9.f), Y - Sx(9.f), Sx(18.f), Sx(18.f), C);
	}
	Box(0, 0, Canvas->ClipX, Canvas->ClipY, FLinearColor(0.6f, 0.f, 0.f, 0.18f * (1.f - Age / 0.8f)));
}

void ADCHUD::TrackOnboarding(ADCCharacter& Me)
{
	if (!bHaveSpawn)
	{
		bHaveSpawn = true;
		SpawnLocation = Me.GetActorLocation();
	}
	bDidMove |= FVector::Dist2D(SpawnLocation, Me.GetActorLocation()) > 400.f;
	bDidShoot |= Me.GetAmmo() < UDCTuning::Get().Rifle.MagazineSize;
	const ADCPlayerState* PS = Me.GetPlayerState<ADCPlayerState>();
	bDidStrike |= PS && PS->CooldownIds.Num() > 0;
	bDidRaise |= Me.IsCameraRaised();
	bDidSnap |= PS && PS->Photos.Num() > 0;
}

void ADCHUD::DrawOnboarding(ADCCharacter& Me)
{
	struct FStep
	{
		bool bDone;
		FString Prompt;
	};
	// Hints name the player's saved bindings (a rebound key shows here, not the default).
	const ADCPlayerController* KeysPC = Cast<ADCPlayerController>(PlayerOwner);
	auto Key = [KeysPC](const TCHAR* Action, const TCHAR* Default) { return KeysPC ? KeysPC->KeyFor(Action).ToUpper() : FString(Default); };
	const FStep Steps[] = {
		{bDidMove, FString::Printf(TEXT("WASD MOVE   %s SPRINT   %s JUMP   %s CROUCH"), *Key(TEXT("Sprint"), TEXT("SHIFT")), *Key(TEXT("Jump"), TEXT("SPACE")), *Key(TEXT("Crouch"), TEXT("CTRL")))},
		{bDidShoot, FString::Printf(TEXT("1 RIFLE   LMB SHOOT   %s RELOAD"), *Key(TEXT("Reload"), TEXT("R")))},
		{bDidStrike, TEXT("2 STRIKE   AIM AT A TOWER   LMB CALL THE ORBITAL LASER")},
		{bDidRaise, TEXT("3 CAMERA   HOLD RMB TO RAISE THE VIEWFINDER")},
		{bDidSnap, TEXT("LMB SNAP THE BLAST - BEST 0.2-0.8s AFTER THE FLASH")},
	};
	const FStep* Next = nullptr;
	int32 Done = 0;
	for (const FStep& Step : Steps)
	{
		Done += Step.bDone ? 1 : 0;
		Next = (!Next && !Step.bDone) ? &Step : Next;
	}
	if (!Next)
	{
		OnboardDoneReal = OnboardDoneReal < 0.0 ? RealNow : OnboardDoneReal;
		if (RealNow - OnboardDoneReal > 4.0)
		{
			return;
		}
	}
	const float W = Canvas->ClipX;
	if (Me.IsCameraRaised())
	{
		// Viewfinder up: only the next step, one small line near the bottom edge, never across the subject (critic).
		if (Next)
		{
			// One line above the LMB SNAP / WINDING label (H - Bar - 110): they overlapped (self-critic CP21).
			const float Bar = FMath::Max(0.f, (Canvas->ClipY - W * 9.f / 16.f) * 0.5f);
			Text(FString::Printf(TEXT("%d/5  %s"), Done + 1, *Next->Prompt), W * 0.5f, Canvas->ClipY - Bar - Sx(165.f), Ink, 0.6f, true);
		}
		return;
	}
	// Low centre, above the viewfinder prompts (they overlapped LMB SNAP): a glass plate with light type.
	const float Y = Canvas->ClipY - Sx(330.f);
	Box(W * 0.5f - Sx(430.f), Y - Sx(6.f), Sx(860.f), Sx(78.f), Paper);
	Box(W * 0.5f - Sx(430.f), Y - Sx(6.f), Sx(5.f), Sx(78.f), Ink);
	Text(TEXT("ORDERS: SHOOT THE DESTRUCTION. PHOTOS PAY FOR BIGGER BOMBS."), W * 0.5f, Y, Ink, 0.65f, true);
	Text(Next ? FString::Printf(TEXT("%d/5  %s"), Done + 1, *Next->Prompt) : FString(TEXT("YOU ARE A WAR PHOTOGRAPHER NOW. GET CLOSER.")), W * 0.5f, Y + Sx(32.f), Ink, 0.75f, true);
}

void ADCHUD::DrawDowned(ADCCharacter& Me)
{
	if (!Me.IsDowned())
	{
		return;
	}
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	// No wash over the view (the Last Frame is shot through it): a red frame at the edges, and the DOWNED plate sits in
	// the upper third, clear of the reticle and below the scoop banner.
	const float Edge = Sx(10.f);
	Box(0, 0, W, Edge, Red);
	Box(0, H - Edge, W, Edge, Red);
	Box(0, 0, Edge, H, Red);
	Box(W - Edge, 0, Edge, H, Red);
	const float Y = Sx(272.f); // below the scoop banner (150-230) with a clear gap
	Box(W * 0.5f - Sx(360.f), Y - Sx(8.f), Sx(720.f), Sx(132.f), Paper);
	Box(W * 0.5f - Sx(360.f), Y - Sx(8.f), Sx(720.f), Sx(5.f), Red);
	Text(TEXT("DOWNED"), W * 0.5f, Y, Red, 1.6f, true);
	const float Left = Me.SelfReviveSecondsLeft();
	Text(Left > 0.f ? FString::Printf(TEXT("HOLD E TO PATCH YOURSELF UP  -  %.0fs"), Left) : FString(TEXT("HOLD ON - A COMRADE CAN REVIVE YOU (THEY HOLD E BESIDE YOU)")),
		W * 0.5f, Y + Sx(60.f), Ink, 0.62f, true);
	Box(W * 0.5f - Sx(200.f), Y + Sx(100.f), Sx(400.f), Sx(12.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.25f));
	Box(W * 0.5f - Sx(200.f), Y + Sx(100.f), Sx(400.f) * Me.GetReviveProgress(), Sx(12.f), Ink);
	if (Me.HasLastFrame())
	{
		// R21: one last shutter from where they fell.
		const float Pulse = 0.75f + 0.25f * FMath::Sin(RealNow * 6.f);
		Box(W * 0.5f - Sx(230.f), H * 0.62f, Sx(460.f), Sx(56.f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f * Pulse));
		Text(TEXT("LAST FRAME  -  LMB"), W * 0.5f, H * 0.62f + Sx(8.f), Night, 1.f, true);
	}
}

void ADCHUD::DrawToast(ADCCharacter& Me)
{
	const float Age = static_cast<float>(RealNow - Me.Feedback().ToastTime);
	if (Age > 2.2f || Me.Feedback().Toast.IsEmpty())
	{
		return;
	}
	Text(Me.Feedback().Toast, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.62f, FLinearColor(1.f, 0.85f, 0.3f, FMath::Clamp((2.2f - Age) * 2.f, 0.f, 1.f)), 0.85f, true);
}

void ADCHUD::DrawScoop(ADCGameState& GS)
{
	const TArray<FDCScoopDef>& Scoops = UDCTuning::Get().Scoops;
	const FDCScoopState& Scoop = GS.Scoop;
	if (!Scoops.IsValidIndex(Scoop.Index))
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float Left = Scoop.EndTime - GS.Now();
	if (Scoop.bDone)
	{
		Text(FString::Printf(TEXT("SCOOP FILED BY %s"), *Scoop.CompletedBy.ToUpper()), W * 0.5f, Sx(196.f), Gold, 0.7f, true);
		return;
	}
	if (Left <= 0.f)
	{
		return;
	}
	const float Pulse = 0.75f + 0.25f * FMath::Sin(RealNow * 6.f);
	// An HQ scoop is a headline, not a danger: magenta-edged glass, light type (red stays for danger).
	Box(W * 0.5f - Sx(420.f), Sx(150.f), Sx(840.f), Sx(80.f), FLinearColor(Paper.R, Paper.G, Paper.B, 0.85f));
	Frame(W * 0.5f - Sx(420.f), Sx(150.f), Sx(840.f), Sx(80.f), Sx(3.f), FLinearColor(Magenta.R, Magenta.G, Magenta.B, Pulse));
	Text(Scoops[Scoop.Index].Text.ToString().ToUpper(), W * 0.5f, Sx(156.f), Ink, 0.75f, true);
	Text(FString::Printf(TEXT("%.0fs  |  REWARD: %d POINTS + SUPPLY POD"), Left, Scoops[Scoop.Index].RewardPoints), W * 0.5f, Sx(194.f), Gold, 0.6f, true);
}
