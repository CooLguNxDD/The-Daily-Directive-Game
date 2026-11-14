// R5 shareable ID card: the builder's turntable captured at Card.Angles yaw steps (the very outfit the character wears
// in play: same DCOutfitBuild recipe), composed on a canvas render target with the call sign and share code in the
// noir look, written as a PNG under Saved/<Card.OutDir>/. Layout numbers are the card's own; colours and copy are data.
#include "DCOutfitPreview.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DCFonts.h"
#include "DCTuning.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

namespace
{
	void Fill(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), GWhiteTexture, FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Tile);
	}

	void Write(UCanvas* C, UFont* Font, const FString& S, float X, float Y, float Scale, const FLinearColor& Color)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(S), Font, Color);
		Item.Scale = FVector2D(Scale, Scale);
		Item.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Item);
	}

	void Picture(UCanvas* C, UTextureRenderTarget2D* Tex, float X, float Y, float W, float H)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), Tex->GetResource(), FVector2D(W, H), FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Opaque; // the capture's alpha is not coverage
		C->DrawItem(Tile);
	}
}

FString ADCOutfitPreview::ExportCard(const FString& CallSign, const FString& ShareCode)
{
	const FDCCardTuning& K = UDCTuning::Get().Card;
	UWorld* World = GetWorld();
	if (!World || !Capture || !Image || K.Angles <= 0)
	{
		return FString();
	}
	// 1. The turn: one capture per yaw step, hero first.
	const FRotator Was = Turn->GetRelativeRotation();
	TArray<UTextureRenderTarget2D*> Views;
	for (int32 i = 0; i <= K.Angles; ++i)
	{
		UTextureRenderTarget2D* Rt = NewObject<UTextureRenderTarget2D>(this);
		Rt->InitAutoFormat(Image->SizeX, Image->SizeY);
		Rt->UpdateResourceImmediate(true);
		const float Yaw = i == 0 ? K.HeroYaw : (i - 1) * 360.f / K.Angles;
		Turn->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		Turn->UpdateComponentToWorld();
		Capture->TextureTarget = Rt;
		Capture->CaptureScene();
		Views.Add(Rt);
	}
	Capture->TextureTarget = Image;
	Turn->SetRelativeRotation(Was);

	// 2. The card.
	UTextureRenderTarget2D* Card = NewObject<UTextureRenderTarget2D>(this);
	Card->RenderTargetFormat = RTF_RGBA8;
	Card->ClearColor = K.Background;
	Card->InitAutoFormat(K.Width, K.Height);
	Card->UpdateResourceImmediate(true);
	UCanvas* C = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Card, C, Size, Ctx);
	if (!C)
	{
		return FString();
	}
	UFont* Head = DCFonts::Get(TEXT("Headline"));
	UFont* Copy = DCFonts::Get(TEXT("Body"));
	const float W = Size.X, H = Size.Y;
	const float S = H / 675.f; // layout drawn for 1200 x 675, scaled with the card height
	Fill(C, 0, 0, W, H, K.Background);
	// Hero: a big portrait in a dark frame with a magenta spine and a cyan floor line.
	const float Hx = 40 * S, Hy = 50 * S, Hw = 440 * S, Hh = 550 * S;
	Fill(C, Hx - 6 * S, Hy - 6 * S, Hw + 12 * S, Hh + 12 * S, K.Panel);
	Picture(C, Views[0], Hx, Hy, Hw, Hh);
	Fill(C, Hx - 6 * S, Hy - 6 * S, 5 * S, Hh + 12 * S, K.AccentA);
	Fill(C, Hx - 6 * S, Hy + Hh + 2 * S, Hw + 12 * S, 4 * S, K.AccentB);
	// Credential column.
	const float X = 520 * S;
	Write(C, Head, K.Title, X, 44 * S, 1.25f * S, K.Ink);
	Write(C, Copy, K.Subtitle, X, 104 * S, 0.85f * S, K.AccentB);
	Fill(C, X, 140 * S, 640 * S, 2 * S, K.AccentA);
	Write(C, Copy, TEXT("CALL SIGN"), X, 160 * S, 0.7f * S, FLinearColor(K.Ink.R, K.Ink.G, K.Ink.B, 0.6f));
	Write(C, Head, CallSign.ToUpper(), X, 184 * S, 2.1f * S, K.Ink);
	Write(C, Copy, TEXT("SHARE CODE"), X, 290 * S, 0.7f * S, FLinearColor(K.Ink.R, K.Ink.G, K.Ink.B, 0.6f));
	// The code is the thing a friend uses: signal yellow (art bible: lime-yellow only for what the player can use).
	Write(C, Head, ShareCode, X, 312 * S, 1.2f * S, UDCTuning::Get().ObjectiveLook.Color);
	// The turn: a strip of thumbnails, each yaw step of the same photographer.
	const float Tw = 96 * S, Th = 120 * S, Gap = 10 * S, Ty = 420 * S;
	for (int32 i = 1; i < Views.Num(); ++i)
	{
		const float Tx = X + (i - 1) * (Tw + Gap);
		Fill(C, Tx - 2 * S, Ty - 2 * S, Tw + 4 * S, Th + 4 * S, K.Panel);
		Picture(C, Views[i], Tx, Ty, Tw, Th);
	}
	Write(C, Copy, TEXT("TURNTABLE"), X, Ty - 26 * S, 0.65f * S, K.AccentB);
	Write(C, Copy, K.Footer, X, 590 * S, 0.62f * S, FLinearColor(K.Ink.R, K.Ink.G, K.Ink.B, 0.75f));
	Write(C, Copy, FString::Printf(TEXT("ISSUED %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M"))), X, 615 * S, 0.62f * S, FLinearColor(K.Ink.R, K.Ink.G, K.Ink.B, 0.5f));
	// The stamp: a glowing word in a frame over the portrait's foot (canvas text cannot tilt; the frame sells it).
	const float Sx0 = Hx + 120 * S, Sy0 = Hy + Hh - 96 * S;
	Fill(C, Sx0 - 10 * S, Sy0 - 8 * S, 310 * S, 3 * S, K.AccentA);
	Fill(C, Sx0 - 10 * S, Sy0 + 44 * S, 310 * S, 3 * S, K.AccentA);
	Write(C, Head, K.Stamp, Sx0, Sy0, 0.95f * S, K.AccentA);
	// Feed scanlines over everything.
	for (float Y = 0; Y < H; Y += 3.f * S)
	{
		Fill(C, 0, Y, W, FMath::Max(1.f, S), FLinearColor(0.f, 0.f, 0.f, K.ScanlineAlpha));
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Ctx);

	// 3. The PNG.
	auto Safe = [](FString In)
	{
		for (TCHAR& Ch : In)
		{
			Ch = FChar::IsAlnum(Ch) ? Ch : TEXT('_');
		}
		return In;
	};
	const FString Dir = FPaths::ProjectSavedDir() / K.OutDir;
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = Dir / FString::Printf(TEXT("%s_%s.png"), *Safe(CallSign), *Safe(ShareCode));
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*Path));
	const bool bOk = Ar && FImageUtils::ExportRenderTarget2DAsPNG(Card, *Ar);
	Ar.Reset();
	UE_LOG(LogTemp, Display, TEXT("DCCARD %s %s (%dx%d, %d angles)"), bOk ? TEXT("wrote") : TEXT("FAILED"), *FPaths::ConvertRelativePathToFull(Path), K.Width, K.Height, K.Angles);
	return bOk ? FPaths::ConvertRelativePathToFull(Path) : FString();
}
