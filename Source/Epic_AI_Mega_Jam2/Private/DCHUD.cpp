#include "DCHUD.h"

#include "Engine/Texture2D.h"
#include "DCPalette.h"

#include "DCCharacter.h"
#include "DCEnvironment.h"
#include "DCGameState.h"
#include "DCPhotoProbe.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
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
	BigFont = GEngine->GetLargeFont();
	SmallFont = GEngine->GetMediumFont();
}

void ADCHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale, bool bCentre, UFont* Font)
{
	UFont* F = Font ? Font : BigFont.Get();
	Scale *= TextBoost;
	float W = 0.f, H = 0.f;
	if (bCentre)
	{
		Canvas->TextSize(F, S, W, H, Scale * UiScale, Scale * UiScale);
	}
	// Drop shadow keeps light text readable over a bloom flash; dark ink on paper needs none.
	if (C.GetLuminance() > 0.35f)
	{
		Canvas->SetDrawColor(FColor(0, 0, 0, static_cast<uint8>(C.A * 200)));
		Canvas->DrawText(F, S, X - W * 0.5f + 2.f, Y + 2.f, Scale * UiScale, Scale * UiScale);
	}
	Canvas->SetDrawColor(C.ToFColor(true));
	Canvas->DrawText(F, S, X - W * 0.5f, Y, Scale * UiScale, Scale * UiScale);
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
	DrawToast(*Me);
}

void ADCHUD::DrawFlashes(ADCGameState& GS, ADCCharacter& Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if (const ADCEnvironment* Env = GS.GetEnvironment())
	{
		const float A = Env->GetFlashAlpha();
		if (A > 0.01f)
		{
			Box(0, 0, W, H, FLinearColor(1.f, 0.97f, 0.9f, A * 0.55f));
		}
	}
	const float Shutter = static_cast<float>(RealNow - Me.Feedback().ShutterTime);
	if (Shutter < 0.18f)
	{
		Box(0, 0, W, H, FLinearColor(1, 1, 1, 0.9f * (1.f - Shutter / 0.18f)));
	}
	if (Me.IsSpotlit())
	{
		Box(0, 0, W, H, FLinearColor(1.f, 1.f, 0.95f, 0.35f + 0.1f * FMath::Sin(RealNow * 20.f)));
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
	Text(TEXT("ORBITAL CALL-IN"), X, Y, Gold, 0.8f);
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
		Text(Line, X, Y, bSel ? (bAfford && Ready <= 0.f ? FLinearColor::White : Red) : FLinearColor(1, 1, 1, 0.5f), 0.65f);
		Y += Sx(26.f);
	}
	Text(TEXT("LMB CALL   Q / WHEEL SWITCH"), X, Y + Sx(6.f), FLinearColor(1, 1, 1, 0.6f), 0.55f);
}

void ADCHUD::DrawViewfinder(ADCCharacter& Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const FLinearColor Line(1, 1, 1, 0.8f);
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
	const FLinearColor Faint(1, 1, 1, 0.18f);
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
		Canvas->TextSize(BigFont, Label, Tw, Th, 0.7f * TextBoost * UiScale, 0.7f * TextBoost * UiScale);
		// Target tags are things the player can use: signal yellow with ink type (red stays for danger, R6).
		// Never under the timer and quota bar at the top.
		const float Ty = FMath::Max(S.Y, Sx(120.f) + Th + 4.f);
		Box(S.X - Tw * 0.5f - 6.f, Ty - Th - 4.f, Tw + 12.f, Th + 4.f, FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f * Pulse));
		Text(Label, S.X, Ty - Th - 3.f, Ink, 0.7f, true);
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

	// Propaganda Points, the currency the shots buy firepower with.
	Box(Sx(24.f), Sx(24.f), Sx(330.f), Sx(74.f), Shade);
	Text(TEXT("PROPAGANDA POINTS"), Sx(36.f), Sx(28.f), Gold, 0.55f);
	Text(FormatPoints(PS ? PS->Points : 0), Sx(36.f), Sx(48.f), FLinearColor::White, 1.4f);

	// Sector and timer.
	const float Left = FMath::Max(0.f, GS.SectorEndTime - GS.Now());
	const FDCSectorDef* Sector = T.Sectors.IsValidIndex(GS.SectorIndex) ? &T.Sectors[GS.SectorIndex] : nullptr;
	const FString Name = Sector ? Sector->Name.ToString().ToUpper() : FString::Printf(TEXT("SECTOR %d"), GS.SectorIndex + 1);
	Text(FString::Printf(TEXT("SECTOR %d/%d  %s"), GS.SectorIndex + 1, FMath::Max(T.Sectors.Num(), 1), *Name), W * 0.5f, Sx(20.f), FLinearColor::White, 0.7f, true);
	Text(FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Left) / 60, FMath::FloorToInt(Left) % 60), W * 0.5f, Sx(46.f), Left < 30.f ? Red : FLinearColor::White, 1.3f, true);

	// HQ quota (R6): the crew's photo score against this sector's target.
	if (Sector && Sector->QuotaPoints > 0)
	{
		const float Q = FMath::Clamp(static_cast<float>(GS.SectorPhotoPoints) / Sector->QuotaPoints, 0.f, 1.f);
		const float Bw = Sx(360.f), Bx = W * 0.5f - Bw * 0.5f, By = Sx(96.f);
		Box(Bx, By, Bw, Sx(14.f), Shade);
		Box(Bx + Sx(2.f), By + Sx(2.f), (Bw - Sx(4.f)) * Q, Sx(10.f), Q >= 1.f ? Gold : Paper);
		Text(FString::Printf(TEXT("HQ QUOTA %s / %s"), *FormatPoints(GS.SectorPhotoPoints), *FormatPoints(Sector->QuotaPoints)), W * 0.5f, By + Sx(14.f), Q >= 1.f ? Gold : FLinearColor::White, 0.5f, true);
	}

	// Film counter is always visible (R7).
	const int32 Shots = PS ? PS->ShotsLeft : 0;
	const int32 Film = PS ? PS->FilmPerSector : 0;
	Box(W - Sx(250.f), Sx(24.f), Sx(226.f), Sx(74.f), Shade);
	Text(TEXT("FILM"), W - Sx(238.f), Sx(28.f), Gold, 0.55f);
	Text(FString::Printf(TEXT("%d / %d"), Shots, Film), W - Sx(238.f), Sx(48.f), Shots > 3 ? FLinearColor::White : Red, 1.4f);

	// Health.
	const float Hp = Me.GetHealth() / FMath::Max(T.Move.MaxHealth, 1.f);
	Box(Sx(24.f), H - Sx(60.f), Sx(300.f), Sx(22.f), Shade);
	Box(Sx(27.f), H - Sx(57.f), Sx(294.f) * Hp, Sx(16.f), Hp > 0.3f ? FLinearColor(0.9f, 0.88f, 0.8f) : Red);
	Text(TEXT("HEALTH"), Sx(24.f), H - Sx(88.f), FLinearColor::White, 0.55f);

	// Slot bar: 1 rifle, 2 strike, 3 camera.
	static const TCHAR* Names[] = {TEXT("1 RIFLE"), TEXT("2 STRIKE"), TEXT("3 CAMERA")};
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bActive = static_cast<int32>(Me.GetSlot()) == i;
		const float X = W * 0.5f - Sx(255.f) + i * Sx(170.f);
		Box(X, H - Sx(70.f), Sx(160.f), Sx(44.f), bActive ? FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f) : Shade);
		Text(Names[i], X + Sx(80.f), H - Sx(62.f), bActive ? Ink : FLinearColor(1, 1, 1, 0.6f), 0.75f, true);
	}
	if (Me.IsDrawing())
	{
		Text(TEXT("DRAWING..."), W * 0.5f, H - Sx(100.f), FLinearColor(1, 1, 1, 0.7f), 0.6f, true);
	}

	// Ammo.
	if (Me.GetSlot() == EDCSlot::Rifle)
	{
		const int32 Ammo = Me.GetAmmo();
		const FLinearColor AC = Ammo > T.Rifle.LowAmmoWarning ? FLinearColor::White : Red;
		Text(FString::Printf(TEXT("%d"), Ammo), W - Sx(200.f), H - Sx(100.f), AC, 2.f);
		Text(FString::Printf(TEXT("/ %d"), Me.GetReserve()), W - Sx(110.f), H - Sx(70.f), FLinearColor::White, 0.8f);
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

void ADCHUD::DrawPhotoCard(UTexture* Image, const FDCPhotoGrade& Grade, float X, float Y, float W, float StampSlam, bool bShowStamp)
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
		Text(TEXT("DEVELOPING..."), X + W * 0.5f, Y + H * 0.45f, Paper, 0.7f, true);
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
	Canvas->TextSize(BigFont, Score, Tw, Th, 1.6f * S * TextBoost * UiScale, 1.6f * S * TextBoost * UiScale);
	const float Bw = Tw + Sx(30.f), Bh = Th * 1.9f + Sx(14.f);
	const float Bx = Cx - Bw * 0.5f, By = Cy - Sx(10.f);
	// Tilt: a per-photo angle from the score, so the same photo always lands the same way.
	const float Tilt = -7.f + static_cast<float>(Grade.Score % 5);
	const FVector Pivot(Cx, By + Bh * 0.5f, 0.f);
	Canvas->Canvas->PushRelativeTransform(FTranslationMatrix(-Pivot) * FRotationMatrix(FRotator(0.f, Tilt, 0.f)) * FTranslationMatrix(Pivot));
	// A signal-yellow plate with ink type and an ink double rule: legible on any print, even on small cards.
	const FLinearColor InkStamp(Ink.R, Ink.G, Ink.B, 0.95f);
	Box(Bx, By, Bw, Bh, FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f));
	Frame(Bx, By, Bw, Bh, Sx(4.f), InkStamp);
	Frame(Bx + Sx(7.f), By + Sx(7.f), Bw - Sx(14.f), Bh - Sx(14.f), Sx(1.5f), InkStamp);
	if (UTexture2D* Worn = Bh > Sx(70.f) ? StampGrit() : nullptr) // small stamps skip the grit: it aliases into noise
	{
		// Paper-coloured speckle over the frame ink (under the type, so small stamps stay legible): pressed by hand.
		DrawTexture(Worn, Bx, By, Bw, Bh, 0, 0, Bw / Sx(256.f), Bh / Sx(128.f), FLinearColor(Paper.R, Paper.G, Paper.B, 0.35f), BLEND_Translucent);
	}
	Text(Score, Cx, By + Sx(4.f), InkStamp, 1.6f * S, true);
	Text(Timing, Cx, By + Sx(4.f) + Th * 1.0f, InkStamp, 0.8f * S, true);
	Canvas->Canvas->PopTransform();
	float Ty = Y + Sx(16.f);
	// Category words as ink type on signal-yellow tags, inset inside the print's border (a blown-out spotlit shot keeps
	// a red tag: that one is a warning).
	const float TagScale = FMath::Clamp(W / Sx(640.f), 0.45f, 1.f);
	for (const FName& Category : Grade.Categories)
	{
		const FString Word = Upper(Category);
		float Cw = 0.f, Ch = 0.f;
		Canvas->TextSize(BigFont, Word, Cw, Ch, 0.8f * TagScale * TextBoost * UiScale, 0.8f * TagScale * TextBoost * UiScale);
		const bool bWarn = Grade.bSpotlit && Category == Grade.Categories[0];
		Box(X + Sx(14.f), Ty, Cw + Sx(12.f), Ch + Sx(2.f), bWarn ? Red : Gold);
		Text(Word, X + Sx(20.f), Ty, bWarn ? Paper : Ink, 0.8f * TagScale);
		Ty += Ch + Sx(6.f);
	}
}

UTexture2D* ADCHUD::StampGrit()
{
	if (Grit)
	{
		return Grit;
	}
	// Speckles and a few worn streaks, alpha only (seeded: the same grit every run).
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
	const float SlideIn = FMath::Clamp(Age / 0.12f, 0.f, 1.f);
	const float SlideOut = FMath::Clamp((Hold - Age) / 0.25f, 0.f, 1.f);
	const float X = Canvas->ClipX - (W + Sx(40.f)) * FMath::Min(SlideIn, SlideOut);
	DrawPhotoCard(Photo->Image, Photo->Grade, X, Sx(130.f), W, Age / 0.15f, true);
}

void ADCHUD::DrawHurt(ADCCharacter& Me)
{
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
		const TCHAR* Prompt;
	};
	const FStep Steps[] = {
		{bDidMove, TEXT("WASD MOVE   SHIFT SPRINT   SPACE JUMP   CTRL CROUCH")},
		{bDidShoot, TEXT("1 RIFLE   LMB SHOOT   R RELOAD")},
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
	const float Y = Canvas->ClipY - Sx(250.f); // low centre: the top band holds the timer, quota and HQ Scoop banner
	Box(W * 0.5f - Sx(430.f), Y - Sx(6.f), Sx(860.f), Sx(78.f), Shade);
	Text(TEXT("ORDERS: SHOOT THE DESTRUCTION. PHOTOS PAY FOR BIGGER BOMBS."), W * 0.5f, Y, Gold, 0.65f, true);
	Text(Next ? FString::Printf(TEXT("%d/5  %s"), Done + 1, Next->Prompt) : FString(TEXT("YOU ARE A WAR PHOTOGRAPHER NOW. GET CLOSER.")), W * 0.5f, Y + Sx(32.f), FLinearColor::White, 0.75f, true);
}

void ADCHUD::DrawDowned(ADCCharacter& Me)
{
	if (!Me.IsDowned())
	{
		return;
	}
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	Box(0, 0, W, H, FLinearColor(0.3f, 0.f, 0.f, 0.35f));
	Text(TEXT("DOWNED"), W * 0.5f, H * 0.4f, Red, 2.f, true);
	const float Left = Me.SelfReviveSecondsLeft();
	Text(Left > 0.f ? FString::Printf(TEXT("HOLD E TO PATCH YOURSELF UP  -  %.0fs"), Left) : FString(TEXT("HOLD ON - A COMRADE CAN REVIVE YOU (THEY HOLD E BESIDE YOU)")),
		W * 0.5f, H * 0.4f + Sx(70.f), FLinearColor::White, 0.7f, true);
	Box(W * 0.5f - Sx(200.f), H * 0.4f + Sx(110.f), Sx(400.f) * Me.GetReviveProgress(), Sx(12.f), FLinearColor::White);
	if (Me.HasLastFrame())
	{
		// R21: one last shutter from where they fell.
		const float Pulse = 0.75f + 0.25f * FMath::Sin(RealNow * 6.f);
		Box(W * 0.5f - Sx(230.f), H * 0.62f, Sx(460.f), Sx(56.f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f * Pulse));
		Text(TEXT("LAST FRAME  -  LMB"), W * 0.5f, H * 0.62f + Sx(8.f), Ink, 1.f, true);
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
	Box(W * 0.5f - Sx(420.f), Sx(150.f), Sx(840.f), Sx(80.f), FLinearColor(Red.R, Red.G, Red.B, 0.85f * Pulse));
	Text(Scoops[Scoop.Index].Text.ToString().ToUpper(), W * 0.5f, Sx(156.f), Paper, 0.75f, true);
	Text(FString::Printf(TEXT("%.0fs  |  REWARD: %d POINTS + SUPPLY POD"), Left, Scoops[Scoop.Index].RewardPoints), W * 0.5f, Sx(194.f), Gold, 0.6f, true);
}
