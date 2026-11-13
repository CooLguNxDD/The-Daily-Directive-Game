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

	// The bloom peak reads: a blast adds its full peak, then decays with the weight.
	TMap<FName, float> Peak;
	Peak.Add(TEXT("Flash"), 60.f);
	const FDCPostResult AtPeak = DCPostStack::Compose(T.PostLayers, Peak, 2);
	TestTrue(TEXT("flash layer carries the bloom peak"), FMath::IsNearlyEqual(AtPeak.Bloom, Base.Bloom + 60.f, 0.01f));

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
	TestTrue(TEXT("halftone shows dots"), Max - Min > 40);
	TestTrue(TEXT("the dim tower beside the white blast is lifted (R5)"), Sum / FMath::Max(1, W - 28) > 60);
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

#endif
