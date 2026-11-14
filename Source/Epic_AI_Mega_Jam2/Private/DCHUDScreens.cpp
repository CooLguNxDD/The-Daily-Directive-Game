// Run-flow screens of the canvas HUD: lobby roster, draft, reel overlay, final cut, vote and the front page.
// Buttons are HUD hit boxes; a click dispatches by the box name prefix through a handler table.
#include "DCHUD.h"
#include "DCPalette.h"

#include "DCAudio.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCReelDirector.h"
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
	// Dark-glass buttons (R25, V9.1: no solid yellow plates): usable ones carry a signal-lime edge and lime type,
	// picked ones a light edge, disabled ones a faint edge and dim type (red stays for danger, R6).
	const FLinearColor Edge = !bEnabled ? FLinearColor(Ink.R, Ink.G, Ink.B, 0.35f) : (bSelected ? Ink : Gold);
	Box(X, Y, W, H, FLinearColor(Paper.R, Paper.G, Paper.B, 0.92f));
	Box(X, Y, W, H, FLinearColor(Edge.R, Edge.G, Edge.B, bEnabled && !bSelected ? 0.12f : 0.f)); // lit glass
	Frame(X, Y, W, H, Sx(bEnabled ? 3.f : 2.f), Edge);
	Text(Label, X + W * 0.5f, Y + H * 0.5f - Sx(14.f), bEnabled ? Edge : FLinearColor(Ink.R, Ink.G, Ink.B, 0.6f), 0.7f, true);
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
		Text(GS.PlayerArray[i] ? GS.PlayerArray[i]->GetPlayerName() : TEXT("?"), X, Y + i * Sx(36.f), Ink, 0.7f);
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
	Text(TEXT("HQ REQUISITION: DRAFT ONE UPGRADE"), W * 0.5f, Sx(120.f), Ink, 1.4f, true);
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
		Box(X, Sx(230.f), ColW, Sx(44.f), FLinearColor(Paper.R, Paper.G, Paper.B, 0.92f));
		Box(X, Sx(270.f), ColW, Sx(3.f), Cyan);
		Text(PoolLabel(Pools[Col]), X + ColW * 0.5f, Sx(236.f), Cyan, 0.7f, true);
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
			Box(X, Y, ColW, CardH, Paper);
			if (bPicked)
			{
				Box(X, Y, ColW, CardH, FLinearColor(Gold.R, Gold.G, Gold.B, 0.12f)); // picked: lime-lit glass, lime edge
				Frame(X, Y, ColW, CardH, Sx(4.f), Gold);
			}
			// Card art (R11) in a square at the left, the words beside it.
			float Tx = X + Sx(16.f);
			int32 Wrap = 52;
			if (const TObjectPtr<UTexture2D>* Icon = CardIcons.Find(Def->Id))
			{
				const float S = CardH - Sx(16.f);
				DrawTexture(*Icon, X + Sx(8.f), Y + Sx(8.f), S, S, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
				Tx = X + S + Sx(22.f);
				Wrap = 40;
			}
			Text(Def->DisplayName.ToString().ToUpper(), Tx, Y + Sx(10.f), bPicked ? Gold : Ink, 0.7f);
			// Description wrapped by words into short lines.
			TArray<FString> Words;
			Def->Description.ToString().ParseIntoArray(Words, TEXT(" "));
			FString Line;
			float Ly = Y + Sx(52.f);
			for (const FString& Word : Words)
			{
				if (Line.Len() + Word.Len() > Wrap)
				{
					Text(Line, Tx, Ly, Ink, 0.5f);
					Ly += Sx(26.f);
					Line.Reset();
				}
				Line += (Line.IsEmpty() ? TEXT("") : TEXT(" ")) + Word;
			}
			Text(Line, Tx, Ly, Ink, 0.5f);
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
		W * 0.5f, Sx(830.f), Ink, 0.75f, true);
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
		// No toast here: the last sector's verdict crossed the clip (review CP26); the front page lists quotas met.
		if (ADCPlayerController* StampPC = Cast<ADCPlayerController>(PlayerOwner))
		{
			DrawStamp(*StampPC); // a second take's stamp slides in like any photo
		}
	};
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	if (!GS.ReelClips.IsValidIndex(GS.ReelIndex) || !PC)
	{
		PaperSheet(0, 0, W, H);
		Text(TEXT("DEVELOPING THE REEL..."), W * 0.5f, H * 0.5f, Ink, 1.f, true);
		return;
	}
	const FDCReelClip& Clip = GS.ReelClips[GS.ReelIndex];
	const UDCTuning& T = UDCTuning::Get();
	const FString Sector = T.Sectors.IsValidIndex(Clip.Photo.Sector) ? T.Sectors[Clip.Photo.Sector].Name.ToString().ToUpper() : TEXT("");
	const float Bar = H * 0.11f;
	// Letterbox bars in dark glass with a thin cyan rule on their inner edge (they were light plates, V9.1).
	Box(0, 0, W, Bar, Paper);
	Box(0, H - Bar, W, Bar, Paper);
	Box(0, Bar - Sx(2.f), W, Sx(2.f), Cyan);
	Box(0, H - Bar, W, Sx(2.f), Cyan);
	Text(FString::Printf(TEXT("BEHIND THE SCENES  |  %s  |  %s"), *Clip.PlayerName.ToUpper(), *Sector), W * 0.5f, Bar * 0.3f, Ink, 0.85f, true);
	Text(FString::Printf(TEXT("CLIP %d / %d"), GS.ReelIndex + 1, GS.ReelClips.Num()), Sx(40.f), H - Bar * 0.7f, Gold, 0.7f);
	if (GS.bRunFailed)
	{
		Text(TEXT("UNIT LOST - FOOTAGE RECOVERED"), W - Sx(40.f) - Sx(330.f), H - Bar * 0.7f, Red, 0.7f);
	}
	// Second take (R16): the hint while the clip replays, and the viewfinder frame while it is raised.
	if (const ADCReelDirector* Director = PC->GetReelDirector())
	{
		if (Director->IsTakeRaised())
		{
			const float M = Sx(60.f), L = Sx(70.f), Tk = Sx(5.f);
			for (const FVector2D& Corner : {FVector2D(M, Bar + M), FVector2D(W - M - L, Bar + M), FVector2D(M, H - Bar - M - Tk), FVector2D(W - M - L, H - Bar - M - Tk)})
			{
				Box(Corner.X, Corner.Y, L, Tk, FLinearColor::White);
			}
			Text(TEXT("SECOND TAKE  -  LMB SNAP"), W * 0.5f, Bar + Sx(20.f), Gold, 0.7f, true);
		}
		else if (Director->CanTakeThisClip())
		{
			Text(TEXT("HOLD RMB: SECOND TAKE FROM ANY ANGLE"), W * 0.5f, H - Bar * 0.4f, Gold, 0.6f, true);
		}
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
		const FLinearColor& Freeze = T.Look.ReelFreezeFlash; // a cool tint, never a white sheet
		Box(0, 0, W, H, FLinearColor(Freeze.R, Freeze.G, Freeze.B, Freeze.A * FMath::Clamp(Age / FMath::Max(T.Run.ReelFreezeSeconds, 0.05f), 0.f, 1.f)));
		Text(TEXT("SNAP"), W * 0.5f, H * 0.5f, Ink, 2.f, true);
		return;
	}
	Box(0, 0, W, H, FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(1.f - Age / 0.4f, 0.f, 1.f)));
	const float CardW = W * 0.6f;
	DrawPhotoCard(PC->GetPhotoImage(Clip.Photo.PhotoId), Clip.Photo.Grade, (W - CardW) * 0.5f, Bar + Sx(30.f), CardW, Age / 0.18f, true, FeedStamp(Clip.Photo.PhotoId, Clip.Photo.ShotTime));
}

// ---------------------------------------------------------------- final cut (R14)

void ADCHUD::DrawFinalCut(ADCGameState& GS, ADCCharacter* Me)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const UDCTuning& T = UDCTuning::Get();
	ADCPlayerController* PC = Cast<ADCPlayerController>(PlayerOwner);
	const ADCPlayerState* PS = PC ? PC->GetPlayerState<ADCPlayerState>() : nullptr;
	// The editor's desk in cream and ink (R6): picks are marked in signal yellow, nothing else is coloured.
	PaperSheet(0, 0, W, H);
	Box(Sx(40.f), Sx(86.f), W - Sx(80.f), Sx(3.f), Ink);
	Text(TEXT("FINAL CUT"), W * 0.5f, Sx(24.f), Ink, 1.4f, true);
	Text(FString::Printf(TEXT("PICK %d SHOTS FOR THE FRONT PAGE. THE CRITIC RANKED THEM; OVERRULE AT WILL.   %s"), T.Run.FinalCutPicks, *Clock(GS.PhaseEndTime - GS.Now())),
		W * 0.5f, Sx(100.f), Ink, 0.7f, true);
	if (!PS || !PC)
	{
		return;
	}
	if (PS->FinalCutChoices.Num() == 0)
	{
		Text(TEXT("NO PHOTOS FILED. HQ IS DISAPPOINTED."), W * 0.5f, H * 0.5f, Ink, 1.f, true);
		return;
	}
	const TArray<int32>& Picks = PS->bFinalCutDone ? PS->FinalCut : PC->GetLocalPicks();
	// Two rows always: second takes add cards, so more columns of smaller cards instead of a third row off screen.
	const int32 Cols = FMath::Max(4, FMath::DivideAndRoundUp(PS->FinalCutChoices.Num(), 2));
	const float Gap = Sx(40.f), CardW = Sx(FMath::Min(400.f, (1760.f - (Cols - 1) * 40.f) / Cols));
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
		const float Y = Sx(180.f) + (i / Cols) * (CardW * 9.f / 16.f + Sx(105.f));
		const int32 Order = Picks.IndexOfByKey(Id);
		DrawPhotoCard(PC->GetPhotoImage(Id), Photo->Grade, X, Y, CardW, 1.f, true, FeedStamp(Photo->PhotoId, Photo->ShotTime));
		Text(FString::Printf(TEXT("CRITIC #%d"), i + 1), X, Y + CardW * 9.f / 16.f + Sx(2.f), Ink, 0.6f);
		if (Order != INDEX_NONE)
		{
			Frame(X - Sx(10.f), Y - Sx(10.f), CardW + Sx(20.f), CardW * 9.f / 16.f + Sx(50.f), Sx(8.f), Gold);
			Box(X + CardW - Sx(96.f), Y + CardW * 9.f / 16.f + Sx(2.f), Sx(92.f), Sx(26.f), Gold);
			Text(FString::Printf(TEXT("PICK %d"), Order + 1), X + CardW - Sx(90.f), Y + CardW * 9.f / 16.f + Sx(2.f), Night, 0.6f);
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
	PaperSheet(0, 0, W, H);
	Text(TEXT("EDITORIAL VOTE"), W * 0.5f, Sx(30.f), Gold, 1.4f, true);
	Text(FString::Printf(TEXT("ONE VOTE FOR A SHOT THAT IS NOT YOURS. VOTES PUT IT ON THE HEADLINE.   %s"), *Clock(GS.PhaseEndTime - GS.Now())), W * 0.5f, Sx(100.f), Ink, 0.7f, true);
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
		DrawPhotoCard(PC->GetPhotoImage(Photo->PhotoId), Photo->Grade, X, Y, CardW, 1.f, true, FeedStamp(Photo->PhotoId, Photo->ShotTime));
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
	PaperSheet(0, 0, W, H);
	const float M = Sx(28.f); // page margin: a dense broadsheet, not a poster on a wall
	const float Bottom = H - Sx(110.f);
	// Body copy: real sentences from data (FrontPageCopy), word-wrapped in small body type, ragged right; the seed
	// picks where each column starts so columns read differently. Greeked bars only when no copy shipped.
	const TArray<FString>& Copy = UDCTuning::Get().FrontPageCopy;
	const FString Who = GS.PlayerArray.Num() > 0 && GS.PlayerArray[0] ? GS.PlayerArray[0]->GetPlayerName().ToUpper() : FString(TEXT("OUR CORRESPONDENT"));
	auto Greek = [this, &Copy, &Who](float X, float Y, float ColW, float ColH, uint32 Seed)
	{
		const float Line = Sx(15.f);
		const float Scale = 0.42f;
		int32 Row = 0;
		FString Cur;
		auto Flush = [&]()
		{
			if ((Row + 1) * Line <= ColH && !Cur.IsEmpty())
			{
				Text(Cur, X, Y + Row * Line, FLinearColor(Ink.R, Ink.G, Ink.B, 0.85f), Scale, false, SmallFont);
			}
			++Row;
			Cur.Reset();
		};
		if (Copy.IsEmpty())
		{
			for (int32 i = 0; (i + 1) * Line <= ColH; ++i)
			{
				Box(X, Y + i * Line, ColW * 0.9f, Sx(5.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.42f));
			}
			return;
		}
		for (int32 n = 0; (Row + 1) * Line <= ColH && n < 64; ++n)
		{
			FString Sentence = Copy[(Seed + n) % Copy.Num()].Replace(TEXT("{PLAYER}"), *Who);
			TArray<FString> Words;
			Sentence.ParseIntoArray(Words, TEXT(" "));
			for (const FString& Word : Words)
			{
				const FString Try = Cur.IsEmpty() ? Word : Cur + TEXT(" ") + Word;
				float Tw = 0.f, Th = 0.f;
				TextSize(SmallFont, Try, Scale * TextBoost * UiScale, Tw, Th);
				if (Tw > ColW && !Cur.IsEmpty())
				{
					Flush();
					Cur = Word;
				}
				else
				{
					Cur = Try;
				}
			}
			if (n % 3 == 2)
			{
				Flush(); // a paragraph break now and then
			}
		}
		Flush();
	};
	Text(TEXT("The Daily Directive"), W * 0.5f, Sx(10.f), Ink, 2.4f, true, MastFont); // descenders clear the rule
	// Ears either side of the masthead.
	Box(M, Sx(30.f), Sx(250.f), Sx(70.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.08f));
	Text(FString::Printf(TEXT("VOL. 1  NO. %d"), GS.RunSeed), M + Sx(12.f), Sx(38.f), Ink, 0.55f);
	Text(TEXT("PRINTED UNDER ORBIT"), M + Sx(12.f), Sx(66.f), Ink, 0.45f);
	Box(Sx(40.f), Sx(120.f), W - Sx(80.f), Sx(4.f), Ink);
	Text(TEXT("SPECIAL REEL EDITION  |  PRICE: ONE PHOTO  |  ALL NEWS IS GOOD NEWS"), W * 0.5f, Sx(130.f), Ink, 0.55f, true);
	Box(Sx(40.f), Sx(162.f), W - Sx(80.f), Sx(2.f), Ink);

	// Headline: paper type on one solid red band (the page's single accent), scaled down until it fits.
	float Scale = 1.8f, Tw = 0.f, Th = 0.f;
	for (; Scale > 0.7f; Scale -= 0.1f)
	{
		TextSize(HeadFont, GS.Headline, Scale * TextBoost * UiScale, Tw, Th);
		if (Tw < W - Sx(120.f))
		{
			break;
		}
	}
	// The headline band: magenta-lit dark glass with a neon edge (a solid magenta slab outshone the hero photo).
	Box(M, Sx(172.f), W - 2.f * M, Sx(84.f), Paper);
	Box(M, Sx(172.f), W - 2.f * M, Sx(84.f), FLinearColor(Magenta.R, Magenta.G, Magenta.B, 0.28f));
	Frame(M, Sx(172.f), W - 2.f * M, Sx(84.f), Sx(3.f), Magenta);
	Text(GS.Headline, W * 0.5f, Sx(172.f) + (Sx(84.f) - Th) * 0.5f, Ink, Scale, true, HeadFont);

	if (PC && GS.PagePicks.IsValidIndex(GS.HeadlineIndex))
	{
		const FDCPagePick& Top = GS.PagePicks[GS.HeadlineIndex];
		const float BigW = W * 0.58f;
		const float Y = Sx(272.f);
		DrawPhotoCard(PC->GetPhotoImage(Top.Photo.PhotoId), Top.Photo.Grade, M, Y, BigW, 1.f, true, FeedStamp(Top.Photo.PhotoId, Top.Photo.ShotTime));
		const float CapY = Y + BigW * 9.f / 16.f + Sx(4.f);
		Text(FString::Printf(TEXT("PHOTO: %s  |  CRITIC %s  |  %d VOTE%s  |  PAGE SCORE %s"), *(Top.Photo.CoStar.IsEmpty() ? Top.PlayerName.ToUpper() : Top.PlayerName.ToUpper() + TEXT(" WITH ") + Top.Photo.CoStar.ToUpper()), *FText::AsNumber(Top.Photo.Grade.Score).ToString(),
			Top.Votes, Top.Votes == 1 ? TEXT("") : TEXT("S"), *FText::AsNumber(Top.PageScore).ToString()), M, CapY, Ink, 0.55f);
		// Three columns of copy under the lead photo, split by hairline rules.
		const float CopyY = CapY + Sx(30.f);
		const float ColGap = Sx(18.f);
		const float ColW = (BigW - 2.f * ColGap) / 3.f;
		for (int32 c = 0; c < 3; ++c)
		{
			const float Cx = M + c * (ColW + ColGap);
			Greek(Cx, CopyY, ColW, Bottom - CopyY - Sx(8.f), 977u * (c + 1) + static_cast<uint32>(GS.RunSeed));
			if (c > 0)
			{
				Box(Cx - ColGap * 0.5f, CopyY, Sx(1.f), Bottom - CopyY - Sx(8.f), FLinearColor(Ink.R, Ink.G, Ink.B, 0.5f));
			}
		}

		// Full edition (R22): up to two runner-up columns down the right, each under its own satirical headline.
		const float SideX = M + BigW + Sx(30.f);
		const float SideW = W - SideX - M;
		Box(SideX - Sx(15.f), Y - Sx(8.f), Sx(2.f), Bottom - Y, Ink);
		TArray<int32> Runners;
		for (int32 i = 0; i < GS.PagePicks.Num(); ++i)
		{
			if (i != GS.HeadlineIndex && !GS.PagePicks[i].Headline.IsEmpty())
			{
				Runners.Add(i);
			}
		}
		Runners.StableSort([&GS](int32 A, int32 B) { return GS.PagePicks[A].PageScore > GS.PagePicks[B].PageScore; });
		Runners.SetNum(FMath::Min(Runners.Num(), 2));
		const float ColumnH = (Bottom - Y) / 2.f;
		const float CardW = FMath::Min(SideW * 0.62f, (ColumnH - Sx(96.f)) * 16.f / 9.f);
		for (int32 k = 0; k < Runners.Num(); ++k)
		{
			const FDCPagePick& P = GS.PagePicks[Runners[k]];
			const float Cy = Y + k * ColumnH;
			Box(SideX, Cy - Sx(8.f), SideW, Sx(2.f), Ink);
			// The column headline shrinks until it fits the column.
			float HeadScale = 0.9f, Hw = 0.f, Hh = 0.f;
			for (; HeadScale > 0.42f; HeadScale -= 0.04f)
			{
				TextSize(HeadFont, P.Headline, HeadScale * TextBoost * UiScale, Hw, Hh);
				if (Hw < SideW)
				{
					break;
				}
			}
			Text(P.Headline, SideX, Cy, Ink, HeadScale, false, HeadFont);
			const float PhotoY = Cy + Hh + Sx(8.f);
			DrawPhotoCard(PC->GetPhotoImage(P.Photo.PhotoId), P.Photo.Grade, SideX, PhotoY, CardW, 1.f, false);
			const FString Byline = P.Photo.CoStar.IsEmpty() ? P.PlayerName.ToUpper() : P.PlayerName.ToUpper() + TEXT(" WITH ") + P.Photo.CoStar.ToUpper();
			Text(FString::Printf(TEXT("PHOTO: %s  |  CRITIC %s  |  %d VOTE%s"), *Byline, *FText::AsNumber(P.Photo.Grade.Score).ToString(), P.Votes, P.Votes == 1 ? TEXT("") : TEXT("S")),
				SideX, PhotoY + CardW * 9.f / 16.f + Sx(4.f), Ink, 0.45f);
			// Copy wraps beside the photo and runs on under it.
			const float BesideX = SideX + CardW + Sx(14.f);
			Greek(BesideX, PhotoY, SideX + SideW - BesideX, CardW * 9.f / 16.f, 31u * (k + 7) + static_cast<uint32>(GS.RunSeed));
			const float UnderY = PhotoY + CardW * 9.f / 16.f + Sx(26.f);
			Greek(SideX, UnderY, SideW, Cy + ColumnH - UnderY - Sx(14.f), 57u * (k + 3) + static_cast<uint32>(GS.RunSeed));
		}
	}
	// The other ear: the edition, stamped in ink (a failed run prints the mock-heroic correction edition).
	Box(W - M - Sx(250.f), Sx(30.f), Sx(250.f), Sx(70.f), GS.bRunFailed ? Magenta : FLinearColor(Ink.R, Ink.G, Ink.B, 0.08f));
	Text(GS.bRunFailed ? TEXT("CORRECTION") : TEXT("LATE FINAL"), W - M - Sx(125.f), Sx(36.f), Ink, 0.7f, true);
	Text(TEXT("EDITION"), W - M - Sx(125.f), Sx(68.f), Ink, 0.45f, true);
	Box(Sx(40.f), H - Sx(110.f), W - Sx(80.f), Sx(2.f), Ink);
	Text(GS.Subhead, W * 0.5f, H - Sx(100.f), Ink, 0.55f, true);
	if (GetNetMode() != NM_Client)
	{
		Button(TEXT("NEW RUN"), TEXT("restart"), W * 0.5f - Sx(160.f), H - Sx(64.f), Sx(320.f), Sx(52.f), true);
	}
}
