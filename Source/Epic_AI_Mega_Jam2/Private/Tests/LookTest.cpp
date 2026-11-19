// Look rules (R5, R19): the post layer stack composes from the shipped data, and the newsprint print keeps a blast
// peak readable. Pure, runs under -nullrhi.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCNewsprint.h"
#include "Rules/DCPostStack.h"
#include "Rules/DCWeather.h"
#include "Rules/DCPhotoRules.h"
#include "DCVoxelCity.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LookFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCPostStackTest, "Epic_AI_Mega_Jam2.Look.PostStack", LookFlags)
bool FDCPostStackTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	for (const TCHAR* Id : {TEXT("Base"), TEXT("Flash"), TEXT("FlashKick"), TEXT("Viewfinder"), TEXT("Damage"), TEXT("Photo"), TEXT("PhotoFlash"), TEXT("Spotlit"), TEXT("Weather"), TEXT("Sector0"), TEXT("Sector1"), TEXT("Sector2")})
	{
		TestTrue(FString::Printf(TEXT("layer %s shipped"), Id), T.PostLayers.ContainsByPredicate([Id](const FDCPostLayer& L) { return L.Id == FName(Id); }));
	}
	TestTrue(TEXT("three scalability presets"), T.PostPresets.Num() >= 3);

	const TMap<FName, float> None;
	const FDCPostResult Base = DCPostStack::Compose(T.PostLayers, None, 2);
	TestTrue(TEXT("base grade is on by default"), Base.Bloom > 0.f && Base.Saturation < 1.f);

	// The bloom peak reads: the hottest blast (weight 1) adds the Flash row's bloom, then decays with the weight.
	const FDCPostLayer* FlashRow = T.PostLayers.FindByPredicate([](const FDCPostLayer& L) { return L.Id == TEXT("Flash"); });
	TMap<FName, float> Peak;
	Peak.Add(TEXT("Flash"), 1.f);
	const FDCPostResult AtPeak = DCPostStack::Compose(T.PostLayers, Peak, 2);
	TestTrue(TEXT("flash layer carries the bloom peak"), FlashRow && FMath::IsNearlyEqual(AtPeak.Bloom, Base.Bloom + FlashRow->Bloom, 0.01f) && AtPeak.Bloom > Base.Bloom * 4.f);
	// Bloom threshold (advisor CP11): the base grade blooms only lights above the window tier; the flash lowers it so
	// the blast keeps its big bloom.
	TestTrue(TEXT("base bloom has a threshold"), Base.BloomThreshold > 0.f);
	TestTrue(TEXT("flash peak blooms more of the frame than the base"), AtPeak.BloomThreshold < Base.BloomThreshold);
	// The screen stops down a little at the peak (advisor CP11): the kick never lifts the frame.
	const FDCPostLayer* KickRow = T.PostLayers.FindByPredicate([](const FDCPostLayer& L) { return L.Id == TEXT("FlashKick"); });
	TestTrue(TEXT("flash kick stops down"), KickRow && KickRow->Exposure <= 0.f);
	// The photo caps the bloom at full flash (R5: the target stays readable).
	Peak.Add(TEXT("Photo"), 1.f);
	const FDCPostLayer* PhotoRow = T.PostLayers.FindByPredicate([](const FDCPostLayer& L) { return L.Id == TEXT("Photo"); });
	TestTrue(TEXT("photo bloom capped at full flash"), PhotoRow && PhotoRow->BloomCap > 0.f && DCPostStack::Compose(T.PostLayers, Peak, 2).Bloom <= PhotoRow->BloomCap + 0.001f);

	// The photo stops down under the flash: exposure falls as the flash weight rises.
	TMap<FName, float> Photo;
	Photo.Add(TEXT("Photo"), 1.f);
	const float Calm = DCPostStack::Compose(T.PostLayers, Photo, 2).Exposure;
	Photo.Add(TEXT("PhotoFlash"), 3.5f);
	TestTrue(TEXT("photo stops down at the peak (R5)"), DCPostStack::Compose(T.PostLayers, Photo, 2).Exposure < Calm - 3.f);

	// Priority order and quality gating.
	TArray<FDCPostLayer> Layers;
	FDCPostLayer A;
	A.Id = TEXT("A");
	A.DefaultWeight = 1.f;
	A.Bloom = 1.f;
	A.MinQuality = 2;
	Layers.Add(A);
	TestEqual(TEXT("high-only layer skipped on low"), DCPostStack::Compose(Layers, None, 0).Bloom, 0.f);
	TestEqual(TEXT("high-only layer on high"), DCPostStack::Compose(Layers, None, 2).Bloom, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCNewsprintTest, "Epic_AI_Mega_Jam2.Look.NewsprintPrint", LookFlags)
bool FDCNewsprintTest::RunTest(const FString&)
{
	const FDCPrintTuning& P = UDCTuning::Get().Camera.Print;
	// A blast-peak frame: 40% blown white, the rest a dim tower (luma ~ 35). Levels must lift the tower.
	const int32 W = 64, H = 36;
	TArray<FColor> Px;
	Px.Init(FColor(30, 34, 40, 255), W * H);
	for (int32 i = 0; i < W * H * 2 / 5; ++i)
	{
		Px[i] = FColor(255, 255, 255, 255);
	}
	float Gain, Offset;
	DCNewsprint::Levels(Px, P, Gain, Offset);
	TestTrue(TEXT("levels stretch a dim frame"), Gain > 1.f);
	TArray<FColor> Printed = Px;
	DCNewsprint::Print(Printed, W, H, P, 7);
	TArray<FColor> Again = Px;
	DCNewsprint::Print(Again, W, H, P, 7);
	TestTrue(TEXT("same photo, same print"), Printed == Again);
	const FColor Border = Printed[0];
	TestTrue(TEXT("paper border"), FMath::Abs(Border.R - P.Paper.R) < 2 && FMath::Abs(Border.B - P.Paper.B) < 2);
	const FColor Rule = Printed[(P.BorderPx) * W + W / 2];
	TestTrue(TEXT("ink rule inside the border"), Rule.R < 60);
	// The dim tower area is no longer a uniform near-black: it varies (halftone dots + grain) and is lifted on average.
	int64 Sum = 0;
	int32 Min = 255, Max = 0;
	for (int32 Y = H - 14; Y < H - 13; ++Y)
	{
		for (int32 X = 14; X < W - 14; ++X)
		{
			const int32 L = Printed[Y * W + X].G;
			Sum += L;
			Min = FMath::Min(Min, L);
			Max = FMath::Max(Max, L);
		}
	}
	// The print's texture is data: halftone dots (newsprint) or feed scanlines (R25 news feed). Check the one in use.
	if (P.HalftoneMix > 0.f && P.DotCell > 0)
	{
		TestTrue(TEXT("halftone shows dots"), Max - Min > 40);
	}
	if (P.ScanlineEvery > 0)
	{
		auto RowMean = [&](int32 Y) { int64 Acc = 0; for (int32 X = 14; X < W - 14; ++X) { Acc += Printed[Y * W + X].G; } return static_cast<float>(Acc) / (W - 28); };
		const int32 ScanY = ((H - 14) / P.ScanlineEvery) * P.ScanlineEvery;
		TestTrue(TEXT("feed scanlines darken every Nth row"), RowMean(ScanY) < RowMean(ScanY + 1) * (1.f - P.ScanlineDark * 0.5f));
	}
	TestTrue(TEXT("the dim tower beside the white blast is lifted (R5)"), Sum / FMath::Max(1, W - 28) > 60);
	if (P.ChromaSplitPx > 0)
	{
		// A white bar on a dark frame: just left of it blue leads (blue is read from the right), just right red trails.
		FDCPrintTuning Clean = P;
		Clean.Grain = 0.f;
		Clean.ScanlineEvery = 0;
		TArray<FColor> Bar;
		Bar.Init(FColor(20, 20, 20, 255), W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 30; X < 34; ++X)
			{
				Bar[Y * W + X] = FColor(240, 240, 240, 255);
			}
		}
		DCNewsprint::Print(Bar, W, H, Clean, 7);
		const FColor Left = Bar[(H / 2) * W + 30 - Clean.ChromaSplitPx];
		const FColor Right = Bar[(H / 2) * W + 33 + Clean.ChromaSplitPx];
		TestTrue(TEXT("feed chroma split: blue fringe on the left"), Left.B > Left.R + 40);
		TestTrue(TEXT("feed chroma split: red fringe on the right"), Right.R > Right.B + 40);
	}
	AddInfo(FString::Printf(TEXT("DCPRINT tower row mean %lld (source 34), min %d max %d, gamma %.2f"), Sum / FMath::Max(1, W - 28), Min, Max, DCNewsprint::MidGamma(Px, P, Gain, Offset)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCWeatherTest, "Epic_AI_Mega_Jam2.Look.SeasonalWeather", LookFlags)
bool FDCWeatherTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TestTrue(TEXT("profiles and seasons shipped"), T.WeatherProfiles.Num() >= 4 && T.Seasons.Num() >= 3);
	for (const FDCWeatherProfile& P : T.WeatherProfiles)
	{
		// Readable for blasts and photos in every profile (R18): never a dark, foggy or blown sky.
		TestTrue(FString::Printf(TEXT("%s keeps the sun"), *P.Id.ToString()), P.SunScale >= 0.4f);
		TestTrue(FString::Printf(TEXT("%s fog stays light"), *P.Id.ToString()), P.FogDensity <= 0.03f);
		TestTrue(FString::Printf(TEXT("%s exposure mild"), *P.Id.ToString()), FMath::Abs(P.Exposure) <= 0.6f);
		TestTrue(FString::Printf(TEXT("%s precipitation kind exists"), *P.Id.ToString()), P.Precipitation.IsNone() || V.FxKinds.ContainsByPredicate([&P](const FDCFxKind& K) { return K.Id == P.Precipitation; }));
		TestTrue(FString::Printf(TEXT("%s cloud and wetness in 0..1"), *P.Id.ToString()), P.CloudCover >= 0.f && P.CloudCover <= 1.f && P.CloudShade >= 0.f && P.CloudShade <= 1.f && P.Wetness >= 0.f && P.Wetness <= 1.f);
		// Rain wets the street; a dry profile with rain falling would read wrong (R18 surface wetness).
		TestTrue(FString::Printf(TEXT("%s rain wets the street"), *P.Id.ToString()), P.Precipitation != FName(TEXT("Rain")) || P.Wetness >= 0.5f);
		TestTrue(FString::Printf(TEXT("%s day light in 0..1"), *P.Id.ToString()), P.DayBlend >= 0.f && P.DayBlend <= 1.f);
	}
	{
		// Day-night cycle driven by the weather (human 2026-11-14): arcs carry the time of day on their keys; across
		// the shipped arcs the sectors reach both the noir night and real daylight, and At() blends the key's day.
		float MinDay = 1.f, MaxDay = 0.f;
		for (const FDCSeasonDef& S : T.Seasons)
		{
			for (const FDCWeatherArc& A : S.Arcs)
			{
				for (const FDCWeatherKey& K : A.Keys)
				{
					MinDay = FMath::Min(MinDay, K.Day);
					MaxDay = FMath::Max(MaxDay, K.Day);
				}
			}
		}
		TestTrue(TEXT("some sector hour is the noir night"), MinDay <= 0.f);
		TestTrue(TEXT("some sector hour is daylight"), MaxDay >= 0.5f);
		const FDCSeasonDef& S0 = T.Seasons[0];
		if (S0.Arcs.Num() > 0 && S0.Arcs[0].Keys.Num() > 0)
		{
			const FDCWeatherArc& A0 = S0.Arcs[0];
			const FDCWeatherProfile* P0 = T.WeatherProfiles.FindByPredicate([&A0](const FDCWeatherProfile& P) { return P.Id == A0.Keys[0].Profile; });
			const float Day0 = DCWeather::At(T.WeatherProfiles, S0, A0, 0.f).Blend.DayBlend;
			TestTrue(TEXT("the first key's time of day shows at the arc start"), P0 && FMath::IsNearlyEqual(Day0, FMath::Clamp(P0->DayBlend + A0.Keys[0].Day, 0.f, 1.f), 0.001f));
		}
	}
	for (const FDCSeasonDef& S : T.Seasons)
	{
		for (const FDCWeatherArc& A : S.Arcs)
		{
			for (const FDCWeatherKey& K : A.Keys)
			{
				TestTrue(FString::Printf(TEXT("%s/%s key %s is a profile"), *S.Id.ToString(), *A.Id.ToString(), *K.Profile.ToString()),
					T.WeatherProfiles.ContainsByPredicate([&K](const FDCWeatherProfile& P) { return P.Id == K.Profile; }));
			}
		}
	}
	TestTrue(TEXT("snow palette for winter sectors"), V.Palette.ContainsByPredicate([](const FDCVoxelMaterial& M) { return M.Id == TEXT("Snow"); }));
	TSet<FName> SeasonsSeen;
	for (int32 Seed = 1; Seed < 40; ++Seed)
	{
		for (int32 Sector = 0; Sector < 3; ++Sector)
		{
			const FDCWeatherState A = ADCVoxelCity::WeatherAt(Seed * 977, Sector, 0.37f);
			const FDCWeatherState B = ADCVoxelCity::WeatherAt(Seed * 977, Sector, 0.37f);
			TestTrue(TEXT("same seed and clock, same weather"), A.Arc == B.Arc && A.Blend.FogDensity == B.Blend.FogDensity && A.Blend.SunScale == B.Blend.SunScale);
			SeasonsSeen.Add(A.Season);
			// The arc changes during play, and smoothly: no jump between two moments 1% apart.
			const FDCWeatherState Start = ADCVoxelCity::WeatherAt(Seed * 977, Sector, 0.f);
			const FDCWeatherState End = ADCVoxelCity::WeatherAt(Seed * 977, Sector, 1.f);
			TestTrue(TEXT("weather changes over the sector"), Start.From != End.To || Start.Blend.SunScale != End.Blend.SunScale);
			float MaxStep = 0.f;
			for (int32 i = 0; i < 100; ++i)
			{
				const FDCWeatherState X = ADCVoxelCity::WeatherAt(Seed * 977, Sector, i / 100.f);
				const FDCWeatherState Y = ADCVoxelCity::WeatherAt(Seed * 977, Sector, (i + 1) / 100.f);
				MaxStep = FMath::Max(MaxStep, FMath::Abs(X.Blend.SunScale - Y.Blend.SunScale));
			}
			TestTrue(TEXT("blend is continuous"), MaxStep < 0.1f);
		}
	}
	TestTrue(TEXT("several seasons across seeds"), SeasonsSeen.Num() >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCCoStarTest, "Epic_AI_Mega_Jam2.Photo.CoStar", LookFlags)
bool FDCCoStarTest::RunTest(const FString&)
{
	// R20: pays by frame fill, never by nearness to death; tag and multiplier only above the minimum fill.
	const FDCPhotoTuning& P = UDCTuning::Get().Photo;
	TestEqual(TEXT("no co-star below the minimum fill"), DCPhotoRules::CoStarMultiplier(P.CoStarMinFill * 0.5f, P), 1.f);
	const float Small = DCPhotoRules::CoStarMultiplier(P.CoStarMinFill, P);
	const float Big = DCPhotoRules::CoStarMultiplier(P.CoStarFullFill, P);
	const float Down = DCPhotoRules::CoStarMultiplier(P.CoStarFullFill, P, true);
	TestTrue(TEXT("a downed co-star still pays, less than a survivor"), Down > 1.f && Down < Big);
	TestTrue(TEXT("a bigger co-star pays more, capped"), Small > 1.f && Big > Small && FMath::IsNearlyEqual(Big, P.CoStarMaxMultiplier) && DCPhotoRules::CoStarMultiplier(1.f, P) <= P.CoStarMaxMultiplier);
	FDCPhotoInputs In;
	In.Visibility = 1.f;
	In.Centering = 1.f;
	const FDCPhotoGrade Alone = DCPhotoRules::GradePhoto(In, P);
	In.CoStarFill = P.CoStarFullFill;
	In.DangerMeters = 1000.f; // far from danger: the co-star pay does not depend on it
	const FDCPhotoGrade With = DCPhotoRules::GradePhoto(In, P);
	TestTrue(TEXT("CO-STAR tag"), With.Categories.Contains(P.CoStarTag) && !Alone.Categories.Contains(P.CoStarTag));
	TestTrue(TEXT("co-star multiplies the score"), With.Score > Alone.Score);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCLastFrameTest, "Epic_AI_Mega_Jam2.Photo.LastFrame", LookFlags)
bool FDCLastFrameTest::RunTest(const FString&)
{
	// R21: tagged, multiplied, and capped so it can match but never beat the sector's best living shot.
	const FDCPhotoTuning& P = UDCTuning::Get().Photo;
	FDCPhotoInputs In;
	In.Visibility = 1.f;
	In.Centering = 1.f;
	In.DangerMeters = 0.f;
	const FDCPhotoGrade Living = DCPhotoRules::GradePhoto(In, P);
	In.bLastFrame = true;
	In.LastFrameCap = 1000000;
	const FDCPhotoGrade Uncapped = DCPhotoRules::GradePhoto(In, P);
	TestTrue(TEXT("LAST FRAME tag and multiplier"), Uncapped.bLastFrame && Uncapped.Categories.Contains(P.LastFrameTag) && Uncapped.Score > Living.Score);
	In.LastFrameCap = Living.Score - 10;
	TestEqual(TEXT("capped at the best living shot"), DCPhotoRules::GradePhoto(In, P).Score, Living.Score - 10);
	In.LastFrameCap = 0;
	TestTrue(TEXT("no living shot yet: no gain over the raw points"), DCPhotoRules::GradePhoto(In, P).Score <= DCPhotoRules::GradePhoto(In, P).RawPoints);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCScreenOnlyLayersTest, "Epic_AI_Mega_Jam2.Look.ScreenOnlyLayers", LookFlags)
bool FDCScreenOnlyLayersTest::RunTest(const FString&)
{
	// The player's screen layers never reach a print (a dying vignette once blackened Last Frame photos): the rows
	// carry the flag, and the photo composition reads it, not a hand-kept name list.
	const TArray<FDCPostLayer>& Layers = UDCTuning::Get().PostLayers;
	for (const TCHAR* Id : {TEXT("Viewfinder"), TEXT("Damage"), TEXT("LastFrame"), TEXT("FlashKick")})
	{
		const FDCPostLayer* Row = Layers.FindByPredicate([Id](const FDCPostLayer& L) { return L.Id == FName(Id); });
		TestTrue(FString::Printf(TEXT("%s row ships"), Id), Row != nullptr);
		TestTrue(FString::Printf(TEXT("%s is screen-only"), Id), Row && Row->bScreenOnly);
	}
	for (const TCHAR* Id : {TEXT("Base"), TEXT("Photo"), TEXT("Weather")})
	{
		const FDCPostLayer* Row = Layers.FindByPredicate([Id](const FDCPostLayer& L) { return L.Id == FName(Id); });
		TestTrue(FString::Printf(TEXT("%s reaches the print"), Id), Row && !Row->bScreenOnly);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCPhotoMeterTest, "Epic_AI_Mega_Jam2.Look.PhotoMeter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCPhotoMeterTest::RunTest(const FString&)
{
	// Advisor CP21: a dark night frame with a dim subject in the middle and a hot blast develops with the subject in
	// the mid range and the blast core still distinct from the blast edge (no flat clip).
	FDCPrintTuning T = UDCTuning::Get().Camera.Print;
	TestTrue(TEXT("the meter is on"), T.MeterKey > 0.f);
	T.bSceneLinear = false; // this case is the display-encoded (LDR) source
	const int32 W = 64, H = 36;
	TArray<FLinearColor> Px;
	Px.Init(FLinearColor(0.06f, 0.06f, 0.08f), W * H); // ~0.2 % linear: noir night
	auto At = [&](int32 X, int32 Y) -> FLinearColor& { return Px[Y * W + X]; };
	for (int32 Y = 12; Y < 24; ++Y)
		for (int32 X = 24; X < 40; ++X)
			At(X, Y) = FLinearColor(0.25f, 0.24f, 0.26f); // the tower face, dim
	At(30, 14) = FLinearColor(0.9f, 0.8f, 0.6f); // blast edge
	At(31, 14) = FLinearColor(1.f, 1.f, 1.f);    // blast core
	TArray<FColor> Out;
	DCNewsprint::Develop(Px, W, H, T, Out);
	const FColor Subject = Out[18 * W + 32];
	const int32 SubjectLuma = (Subject.R + Subject.G + Subject.B) / 3;
	TestTrue(FString::Printf(TEXT("subject lands mid-range (%d)"), SubjectLuma), SubjectLuma >= 70 && SubjectLuma <= 190);
	const FColor Edge = Out[14 * W + 30], Core = Out[14 * W + 31];
	TestTrue(FString::Printf(TEXT("blast core and edge stay distinct (%d vs %d)"), Core.G, Edge.G), Core.G > Edge.G);
	TestTrue(TEXT("gain bounded"), DCNewsprint::MeterGain(Px, W, H, T) <= FMath::Pow(2.f, T.MeterMaxStops) + 0.001f);
	{
		// Advisor CP28: the scene-linear (HDR) source keeps headroom: a 0.03 subject on a 0.004 night lands mid-range,
		// and highlights at 4 and 16 (above 1) still print distinct instead of clipping together.
		FDCPrintTuning L = UDCTuning::Get().Camera.Print;
		L.bSceneLinear = true;
		TArray<FLinearColor> Hdr;
		Hdr.Init(FLinearColor(0.004f, 0.004f, 0.005f), W * H);
		for (int32 Y = 12; Y < 24; ++Y)
			for (int32 X = 24; X < 40; ++X)
				Hdr[Y * W + X] = FLinearColor(0.03f, 0.03f, 0.032f);
		Hdr[14 * W + 30] = FLinearColor(4.f, 4.f, 4.f);
		Hdr[14 * W + 31] = FLinearColor(16.f, 16.f, 16.f);
		TArray<FColor> HOut;
		DCNewsprint::Develop(Hdr, W, H, L, HOut);
		const FColor S = HOut[18 * W + 32];
		const int32 SL = (S.R + S.G + S.B) / 3;
		TestTrue(FString::Printf(TEXT("HDR subject lands mid-range (%d)"), SL), SL >= 60 && SL <= 190);
		TestTrue(FString::Printf(TEXT("HDR 4 and 16 print distinct (%d vs %d)"), HOut[14 * W + 30].G, HOut[14 * W + 31].G), HOut[14 * W + 31].G > HOut[14 * W + 30].G);
	}
	return true;
}

#endif
