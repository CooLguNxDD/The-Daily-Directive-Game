// Run-flow screens of the canvas HUD: lobby roster, draft, reel overlay, final cut, vote and the front page.
// Buttons are HUD hit boxes; a click dispatches by the box name prefix through a handler table.
#include "DCHUD.h"
#include "DCPalette.h"

#include "DCAudio.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "Engine/Canvas.h"
#include "Engine/Texture.h"
#include "Engine/World.h"

using namespace DCPalette;

namespace DCHudScreens
{

	const ADCPlayerState* FindOwner(const ADCGameState& GS, int32 PhotoId)
	{
		for (const APlayerState* P : GS.PlayerArray)
		{
			const ADCPlayerState* PS = Cast<ADCPlayerState>(P);
			if (PS && PS->FindPhoto(PhotoId))
			{
				return PS;
			}
		}
		return nullptr;
	}

	FString PoolLabel(FName Pool)
	{
		static const TMap<FName, FString> Labels = {
			{TEXT("Stratagem"), TEXT("CINEMA STRATAGEM")},
			{TEXT("Gear"), TEXT("CAMERA GEAR")},
			{TEXT("Mutator"), TEXT("CITY MUTATOR")},
		};
		const FString* Found = Labels.Find(Pool);
		return Found ? *Found : Pool.ToString().ToUpper();
	}

	FString Clock(float Seconds)
	{
		const int32 S = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
	}
}

using namespace DCHudScreens;

bool ADCHUD::Button(const FString& Label, FName Id, float X, float Y, float W, float H, bool bEnabled, bool bSelected)
{
	const FLinearColor Fill = !bEnabled ? FLinearColor(0.3f, 0.3f, 0.3f, 0.8f) : (bSelected ? Gold : Red);
	Box(X, Y, W, H, Fill);
	Text(Label, X + W * 0.5f, Y + H * 0.5f - Sx(14.f), bSelected ? Ink : Paper, 0.7f, true);
	if (bEnabled)
	{
		AddHitBox(FVector2D(X, Y), FVector2D(W, H), Id, true);
	}
	return bEnabled;
}

void ADCHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	FString Kind, Arg;
	if (!PC || !BoxName.ToString().Split(TEXT("|"), &Kind, &Arg))
	{
		Kind = BoxName.ToString();
	}
	using FClick = TFunction<void(ADCPlayerController&, const FString&)>;
	static const TMap<FString, FClick> Clicks = {
		{TEXT("pick"), [](ADCPlayerController& P, const FString& A) { P.RequestPickUpgrade(FName(*A)); }},
		{TEXT("cut"), [](ADCPlayerController& P, const FString& A) { P.ToggleFinalCutPick(FCString::Atoi(*A)); }},
		{TEXT("submit"), [](ADCPlayerController& P, const FString&) { P.RequestSubmitFinalCut(); }},
		{TEXT("vote"), [](ADCPlayerController& P, const FString& A) { P.RequestVote(FCString::Atoi(*A)); }},
		{TEXT("restart"), [](ADCPlayerController& P, const FString&) { P.RequestRestart(); }},
	};
	if (const FClick* Fn = PC ? Clicks.Find(Kind) : nullptr)
	{
		(*Fn)(*PC, Arg);
	}
}

// ---------------------------------------------------------------- lobby

void ADCHUD::DrawLobby(ADCGameState& GS, ADCCharacter* Me)
{
	if (GetNetMode() == NM_Standalone)
	{
		return; // the title menu (Slate) covers it
	}
	const float H = Canvas->ClipY;
	const float X = Sx(40.f), Y = H - Sx(80.f) - GS.PlayerArray.Num() * Sx(36.f);
	Box(X - Sx(12.f), Y - Sx(50.f), Sx(420.f), GS.PlayerArray.Num() * Sx(36.f) + Sx(70.f), Dim);
	Text(FString::Printf(TEXT("CREW %d/4"), GS.PlayerArray.Num()), X, Y - Sx(44.f), Gold, 0.7f);
	for (int32 i = 0; i < GS.PlayerArray.Num(); ++i)
	{
		Text(GS.PlayerArray[i] ? GS.PlayerArray[i]->GetPlayerName() : TEXT("?"), X, Y + i * Sx(36.f), Paper, 0.7f);
	}
}

// ---------------------------------------------------------------- draft (R10)

void ADCHUD::DrawDraft(ADCGameState& GS, ADCCharacter* Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const UDCTuning& T = UDCTuning::Get();
	const ADCPlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<ADCPlayerState>() : nullptr;
	Box(0, 0, W, H, Dim);
	Text(FString::Printf(TEXT("SECTOR %d SECURED"), GS.SectorIndex + 1), W * 0.5f, Sx(70.f), Gold, 1.f, true);
	Text(TEXT("HQ REQUISITION: DRAFT ONE UPGRADE"), W * 0.5f, Sx(120.f), Paper, 1.4f, true);
	Text(Clock(GS.PhaseEndTime - GS.Now()), W * 0.5f, Sx(190.f), FLinearColor::White, 0.9f, true);
	if (!PS)
	{
		return;
	}
	// Three columns, one per pool; each column lists that pool's open options.
	TArray<FName> Pools;
	for (const FName& Id : PS->DraftOffer)
	{
		if (const FDCUpgradeDef* Def = T.FindUpgrade(Id))
		{
			Pools.AddUnique(Def->Pool);
		}
	}
	const float ColW = Sx(560.f), Gap = Sx(30.f), CardH = Sx(150.f);
	const float X0 = W * 0.5f - (Pools.Num() * ColW + (Pools.Num() - 1) * Gap) * 0.5f;
	for (int32 Col = 0; Col < Pools.Num(); ++Col)
	{
		const float X = X0 + Col * (ColW + Gap);
		Box(X, Sx(230.f), ColW, Sx(44.f), Red);
		Text(PoolLabel(Pools[Col]), X + ColW * 0.5f, Sx(236.f), Paper, 0.7f, true);
		int32 Row = 0;
		for (const FName& Id : PS->DraftOffer)
		{
			const FDCUpgradeDef* Def = T.FindUpgrade(Id);
			if (!Def || Def->Pool != Pools[Col])
			{
				continue;
			}
			const bool bPicked = PS->Upgrades.Contains(Def->Id);
			const float Y = Sx(290.f) + Row++ * (CardH + Sx(14.f));
			Box(X, Y, ColW, CardH, bPicked ? Gold : Paper);
			Text(Def->DisplayName.ToString().ToUpper(), X + Sx(16.f), Y + Sx(10.f), Ink, 0.7f);
			// Description wrapped by words into short lines.
			TArray<FString> Words;
			Def->Description.ToString().ParseIntoArray(Words, TEXT(" "));
			FString Line;
			float Ly = Y + Sx(52.f);
			for (const FString& Word : Words)
			{
				if (Line.Len() + Word.Len() > 52)
				{
					Text(Line, X + Sx(16.f), Ly, Ink, 0.5f);
					Ly += Sx(26.f);
					Line.Reset();
				}
				Line += (Line.IsEmpty() ? TEXT("") : TEXT(" ")) + Word;
			}
			Text(Line, X + Sx(16.f), Ly, Ink, 0.5f);
			if (!PS->bDraftDone)
			{
				AddHitBox(FVector2D(X, Y), FVector2D(ColW, CardH), FName(*FString::Printf(TEXT("pick|%s"), *Def->Id.ToString())), true);
			}
		}
	}
	int32 Done = 0;
	for (const APlayerState* P : GS.PlayerArray)
	{
		const ADCPlayerState* D = Cast<ADCPlayerState>(P);
		Done += D && D->bDraftDone ? 1 : 0;
	}
	Text(PS->bDraftDone ? FString::Printf(TEXT("WAITING FOR THE CREW  %d/%d"), Done, GS.PlayerArray.Num()) : FString(TEXT("CLICK ONE CARD. EACH UPGRADE CHANGES HOW YOU PLAY.")),
		W * 0.5f, Sx(830.f), Paper, 0.75f, true);
	FString Owned;
	for (const FName& Id : PS->Upgrades)
	{
		const FDCUpgradeDef* Def = T.FindUpgrade(Id);
		Owned += (Owned.IsEmpty() ? TEXT("") : TEXT("  |  ")) + (Def ? Def->DisplayName.ToString() : Id.ToString());
	}
	if (!Owned.IsEmpty())
	{
		Text(TEXT("YOUR KIT: ") + Owned.ToUpper(), W * 0.5f, Sx(880.f), Gold, 0.6f, true);
	}
}

// ---------------------------------------------------------------- reel (R13)

void ADCHUD::DrawReel(ADCGameState& GS, ADCCharacter* Me)
{
	ON_SCOPE_EXIT
	{
		if (Me)
		{
			DrawToast(*Me); // e.g. the last sector's quota verdict, said as the reel starts
		}
	};
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	if (!GS.ReelClips.IsValidIndex(GS.ReelIndex) || !PC)
	{
		Box(0, 0, W, H, Ink);
		Text(TEXT("DEVELOPING THE REEL..."), W * 0.5f, H * 0.5f, Paper, 1.f, true);
		return;
	}
	const FDCReelClip& Clip = GS.ReelClips[GS.ReelIndex];
	const UDCTuning& T = UDCTuning::Get();
	const FString Sector = T.Sectors.IsValidIndex(Clip.Photo.Sector) ? T.Sectors[Clip.Photo.Sector].Name.ToString().ToUpper() : TEXT("");
	const float Bar = H * 0.11f;
	Box(0, 0, W, Bar, Ink);
	Box(0, H - Bar, W, Bar, Ink);
	Text(FString::Printf(TEXT("BEHIND THE SCENES  |  %s  |  %s"), *Clip.PlayerName.ToUpper(), *Sector), W * 0.5f, Bar * 0.3f, Paper, 0.85f, true);
	Text(FString::Printf(TEXT("CLIP %d / %d"), GS.ReelIndex + 1, GS.ReelClips.Num()), Sx(40.f), H - Bar * 0.7f, Gold, 0.7f);
	if (GS.bRunFailed)
	{
		Text(TEXT("UNIT LOST - FOOTAGE RECOVERED"), W - Sx(40.f) - Sx(330.f), H - Bar * 0.7f, Red, 0.7f);
	}
	if (GS.ReelStage == 0)
	{
		if (ReelStageSeen != 0 || ReelIndexSeen != GS.ReelIndex)
		{
			ReelStageSeen = 0;
			ReelIndexSeen = GS.ReelIndex;
			ReelStageReal = RealNow;
			DCAudio::Play2D(this, TEXT("Whoosh"), 0.9f);
		}
		Text(FString::Printf(TEXT("SLOW MOTION %.1fx"), T.Run.ReelSlowMo), W * 0.5f, H - Bar * 0.7f, FLinearColor::White, 0.7f, true);
		return;
	}
	if (ReelStageSeen != GS.ReelStage || ReelIndexSeen != GS.ReelIndex)
	{
		ReelStageSeen = GS.ReelStage;
		ReelIndexSeen = GS.ReelIndex;
		ReelStageReal = RealNow;
		// Sound per reel stage: whoosh into the clip, flash at the freeze, the stamp on the photo.
		static const FName StageCues[] = {TEXT("Whoosh"), TEXT("Flash"), TEXT("Stamp")};
		DCAudio::Play2D(this, StageCues[FMath::Min<int32>(GS.ReelStage, UE_ARRAY_COUNT(StageCues) - 1)], 0.9f);
	}
	const float Age = static_cast<float>(RealNow - ReelStageReal);
	if (GS.ReelStage == 1)
	{
		// Frozen at the shutter: the white flash builds.
		Box(0, 0, W, H, FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(Age / FMath::Max(T.Run.ReelFreezeSeconds, 0.05f), 0.f, 1.f) * 0.95f));
		Text(TEXT("SNAP"), W * 0.5f, H * 0.5f, Ink, 2.f, true);
		return;
	}
	Box(0, 0, W, H, FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(1.f - Age / 0.4f, 0.f, 1.f)));
	const float CardW = W * 0.6f;
	DrawPhotoCard(PC->GetPhotoImage(Clip.Photo.PhotoId), Clip.Photo.Grade, (W - CardW) * 0.5f, Bar + Sx(30.f), CardW, Age / 0.18f, true);
}

// ---------------------------------------------------------------- final cut (R14)

void ADCHUD::DrawFinalCut(ADCGameState& GS, ADCCharacter* Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const UDCTuning& T = UDCTuning::Get();
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
	Box(0, 0, W, H, Ink);
	Text(TEXT("FINAL CUT"), W * 0.5f, Sx(30.f), Gold, 1.4f, true);
	Text(FString::Printf(TEXT("PICK %d SHOTS FOR THE FRONT PAGE. THE CRITIC RANKED THEM; OVERRULE AT WILL.   %s"), T.Run.FinalCutPicks, *Clock(GS.PhaseEndTime - GS.Now())),
		W * 0.5f, Sx(100.f), Paper, 0.7f, true);
	if (!PS || !PC)
	{
		return;
	}
	if (PS->FinalCutChoices.Num() == 0)
	{
		Text(TEXT("NO PHOTOS FILED. HQ IS DISAPPOINTED."), W * 0.5f, H * 0.5f, Red, 1.f, true);
		return;
	}
	const TArray<int32>& Picks = PS->bFinalCutDone ? PS->FinalCut : PC->GetLocalPicks();
	const int32 Cols = 4;
	const float CardW = Sx(400.f), Gap = Sx(40.f);
	const float X0 = W * 0.5f - (Cols * CardW + (Cols - 1) * Gap) * 0.5f;
	for (int32 i = 0; i < PS->FinalCutChoices.Num(); ++i)
	{
		const int32 Id = PS->FinalCutChoices[i];
		const FDCPhotoRecord* Photo = PS->FindPhoto(Id);
		if (!Photo)
		{
			continue;
		}
		const float X = X0 + (i % Cols) * (CardW + Gap);
		const float Y = Sx(180.f) + (i / Cols) * Sx(330.f);
		const int32 Order = Picks.IndexOfByKey(Id);
		DrawPhotoCard(PC->GetPhotoImage(Id), Photo->Grade, X, Y, CardW, 1.f, true);
		Text(FString::Printf(TEXT("CRITIC #%d"), i + 1), X, Y + CardW * 9.f / 16.f + Sx(2.f), Ink, 0.6f);
		if (Order != INDEX_NONE)
		{
			Frame(X - Sx(10.f), Y - Sx(10.f), CardW + Sx(20.f), CardW * 9.f / 16.f + Sx(50.f), Sx(8.f), Gold);
			Text(FString::Printf(TEXT("PICK %d"), Order + 1), X + CardW - Sx(90.f), Y + CardW * 9.f / 16.f + Sx(2.f), Red, 0.6f);
		}
		if (!PS->bFinalCutDone)
		{
			AddHitBox(FVector2D(X, Y), FVector2D(CardW, CardW * 9.f / 16.f), FName(*FString::Printf(TEXT("cut|%d"), Id)), true);
		}
	}
	const int32 Want = FMath::Min(T.Run.FinalCutPicks, PS->FinalCutChoices.Num());
	if (PS->bFinalCutDone)
	{
		Text(TEXT("SENT TO THE PRESS. WAITING FOR THE CREW..."), W * 0.5f, H - Sx(110.f), Gold, 0.9f, true);
		return;
	}
	Button(FString::Printf(TEXT("SEND TO PRESS  %d/%d"), Picks.Num(), Want), TEXT("submit"), W * 0.5f - Sx(250.f), H - Sx(130.f), Sx(500.f), Sx(70.f), Picks.Num() == Want);
}

// ---------------------------------------------------------------- vote (R15)

void ADCHUD::DrawVote(ADCGameState& GS, ADCCharacter* Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	const ADCPlayerState* Mine = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
	Box(0, 0, W, H, Ink);
	Text(TEXT("EDITORIAL VOTE"), W * 0.5f, Sx(30.f), Gold, 1.4f, true);
	Text(FString::Printf(TEXT("ONE VOTE FOR A SHOT THAT IS NOT YOURS. VOTES PUT IT ON THE HEADLINE.   %s"), *Clock(GS.PhaseEndTime - GS.Now())), W * 0.5f, Sx(100.f), Paper, 0.7f, true);
	if (!PC || !Mine)
	{
		return;
	}
	TArray<TPair<const ADCPlayerState*, int32>> Shots;
	for (const APlayerState* P : GS.PlayerArray)
	{
		const ADCPlayerState* PS = Cast<ADCPlayerState>(P);
		if (PS && PS != Mine)
		{
			for (int32 Id : PS->FinalCut)
			{
				Shots.Add(TPair<const ADCPlayerState*, int32>(PS, Id));
			}
		}
	}
	const int32 Cols = 4;
	const float CardW = Sx(400.f), Gap = Sx(40.f);
	const float X0 = W * 0.5f - (Cols * CardW + (Cols - 1) * Gap) * 0.5f;
	for (int32 i = 0; i < Shots.Num() && i < 12; ++i)
	{
		const FDCPhotoRecord* Photo = Shots[i].Key->FindPhoto(Shots[i].Value);
		if (!Photo)
		{
			continue;
		}
		const float X = X0 + (i % Cols) * (CardW + Gap);
		const float Y = Sx(170.f) + (i / Cols) * Sx(300.f);
		DrawPhotoCard(PC->GetPhotoImage(Photo->PhotoId), Photo->Grade, X, Y, CardW, 1.f, true);
		Text(Shots[i].Key->GetPlayerName().ToUpper(), X, Y + CardW * 9.f / 16.f + Sx(2.f), Ink, 0.6f);
		if (Mine->VotedFor == Photo->PhotoId)
		{
			Frame(X - Sx(10.f), Y - Sx(10.f), CardW + Sx(20.f), CardW * 9.f / 16.f + Sx(50.f), Sx(8.f), Gold);
		}
		if (Mine->VotedFor == 0)
		{
			AddHitBox(FVector2D(X, Y), FVector2D(CardW, CardW * 9.f / 16.f), FName(*FString::Printf(TEXT("vote|%d"), Photo->PhotoId)), true);
		}
	}
	Text(Mine->VotedFor == 0 ? FString(TEXT("CLICK A PHOTO TO VOTE")) : FString(TEXT("VOTE CAST. WAITING FOR THE CREW...")), W * 0.5f, H - Sx(90.f), Gold, 0.9f, true);
}

// ---------------------------------------------------------------- front page (R15)

void ADCHUD::DrawFrontPage(ADCGameState& GS, ADCCharacter* Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	if (!bPressPlayed)
	{
		bPressPlayed = true;
		DCAudio::Play2D(this, TEXT("Press"), 1.f); // the printing press rolls
	}
	Box(0, 0, W, H, Paper);
	Text(TEXT("THE DAILY DIRECTIVE"), W * 0.5f, Sx(18.f), Ink, 2.6f, true);
	Box(Sx(40.f), Sx(120.f), W - Sx(80.f), Sx(4.f), Ink);
	Text(TEXT("SPECIAL REEL EDITION  |  PRICE: ONE PHOTO  |  ALL NEWS IS GOOD NEWS"), W * 0.5f, Sx(130.f), Ink, 0.55f, true);
	Box(Sx(40.f), Sx(162.f), W - Sx(80.f), Sx(2.f), Ink);

	// Headline: scaled down until it fits the page width.
	float Scale = 2.f, Tw = 0.f, Th = 0.f;
	for (; Scale > 0.7f; Scale -= 0.1f)
	{
		Canvas->TextSize(BigFont, GS.Headline, Tw, Th, Scale * TextBoost * UiScale, Scale * TextBoost * UiScale);
		if (Tw < W - Sx(100.f))
		{
			break;
		}
	}
	Text(GS.Headline, W * 0.5f, Sx(176.f), Red, Scale, true);

	if (PC && GS.PagePicks.IsValidIndex(GS.HeadlineIndex))
	{
		const FDCPagePick& Top = GS.PagePicks[GS.HeadlineIndex];
		const float BigW = W * 0.56f;
		const float Y = Sx(290.f);
		DrawPhotoCard(PC->GetPhotoImage(Top.Photo.PhotoId), Top.Photo.Grade, Sx(60.f), Y, BigW, 1.f, true);
		Text(FString::Printf(TEXT("PHOTO: %s  |  CRITIC %s  |  %d VOTE%s  |  PAGE SCORE %s"), *Top.PlayerName.ToUpper(), *FText::AsNumber(Top.Photo.Grade.Score).ToString(),
			Top.Votes, Top.Votes == 1 ? TEXT("") : TEXT("S"), *FText::AsNumber(Top.PageScore).ToString()), Sx(60.f), Y + BigW * 9.f / 16.f + Sx(6.f), Ink, 0.55f);

		// The other picks, smaller, down the right column.
		const float SideX = Sx(60.f) + BigW + Sx(50.f);
		const float SideW = W - SideX - Sx(60.f);
		const float CardW = FMath::Min(SideW, Sx(360.f));
		int32 Shown = 0;
		for (int32 i = 0; i < GS.PagePicks.Num() && Shown < 4; ++i)
		{
			if (i == GS.HeadlineIndex)
			{
				continue;
			}
			const FDCPagePick& P = GS.PagePicks[i];
			const float Cy = Sx(290.f) + Shown * (CardW * 9.f / 16.f + Sx(64.f));
			const float Cx = SideX + (Shown % 2) * 0.f;
			DrawPhotoCard(PC->GetPhotoImage(P.Photo.PhotoId), P.Photo.Grade, Cx, Cy, CardW, 1.f, false);
			Text(FString::Printf(TEXT("%s  |  CRITIC %s  |  %d VOTE%s  |  PAGE %s"), *P.PlayerName.ToUpper(), *FText::AsNumber(P.Photo.Grade.Score).ToString(), P.Votes,
				P.Votes == 1 ? TEXT("") : TEXT("S"), *FText::AsNumber(P.PageScore).ToString()), Cx, Cy + CardW * 9.f / 16.f + Sx(4.f), Ink, 0.5f);
			++Shown;
		}
	}
	Box(Sx(40.f), H - Sx(110.f), W - Sx(80.f), Sx(2.f), Ink);
	Text(GS.Subhead, W * 0.5f, H - Sx(100.f), Ink, 0.55f, true);
	if (GetNetMode() != NM_Client)
	{
		Button(TEXT("NEW RUN"), TEXT("restart"), W * 0.5f - Sx(160.f), H - Sx(64.f), Sx(320.f), Sx(52.f), true);
	}
}
