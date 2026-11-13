// Look rules (R5, R19): the post layer stack composes from the shipped data, and the newsprint print keeps a blast
// peak readable. Pure, runs under -nullrhi.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCNewsprint.h"
#include "Rules/DCPostStack.h"

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

#endif
