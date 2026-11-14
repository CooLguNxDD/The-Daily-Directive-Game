// Photographer outfits (R23): share codes round-trip, untrusted codes are rejected with a reason, every shipped part
// has a model that loads, and cycling wraps. Pure (the model check reads the shipped .dcvox files).
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCOutfit.h"
#include "Voxel/DCVoxelModel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOutfitCodeTest, "Epic_AI_Mega_Jam2.Outfit.ShareCode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOutfitCodeTest::RunTest(const FString&)
{
	const UDCTuning& T = UDCTuning::Get();
	TestTrue(TEXT("slots ship"), T.OutfitSlots.Num() >= 4);
	TestTrue(TEXT("swatches ship"), T.OutfitSwatches.Num() >= 2);
	// Every combination of the first two parts per slot round-trips.
	FDCOutfit O = DCOutfit::Default(T.OutfitSlots);
	for (int32 s = 0; s < T.OutfitSlots.Num(); ++s)
	{
		for (int32 Step = 0; Step < 3; ++Step)
		{
			DCOutfit::Cycle(O, s, 1, T.OutfitSlots, T.OutfitParts);
			O.Main = static_cast<uint8>((O.Main + 1) % T.OutfitSwatches.Num());
			FDCOutfit Back;
			FString Error;
			const FString Code = DCOutfit::Encode(O);
			TestTrue(FString::Printf(TEXT("%s decodes (%s)"), *Code, *Error), DCOutfit::Decode(Code, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), Back, Error));
			TestTrue(FString::Printf(TEXT("%s round-trips"), *Code), Back == O);
			TestTrue(TEXT("lower case and spaces still load"), DCOutfit::Decode(TEXT("  ") + Code.ToLower() + TEXT(" "), T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), Back, Error));
		}
	}
	// Cycling wraps both ways.
	FDCOutfit W = DCOutfit::Default(T.OutfitSlots);
	DCOutfit::Cycle(W, 0, -1, T.OutfitSlots, T.OutfitParts);
	TestEqual(TEXT("cycle back wraps to the last part"), static_cast<int32>(W.Parts[0]), DCOutfit::PartsOf(T.OutfitSlots[0], T.OutfitParts).Num() - 1);
	// Untrusted codes: every bad one is rejected with a reason and leaves the output alone.
	const FString Good = DCOutfit::Encode(DCOutfit::Default(T.OutfitSlots));
	FString Typo = Good;
	Typo[Typo.Len() - 2] = Typo[Typo.Len() - 2] == TEXT('1') ? TEXT('2') : TEXT('1');
	FDCOutfit TooFar = DCOutfit::Default(T.OutfitSlots);
	TooFar.Parts[0] = 200;
	FDCOutfit BadSwatch = DCOutfit::Default(T.OutfitSlots);
	BadSwatch.Main = 250;
	FDCOutfit FewerSlots;
	FewerSlots.Parts = {0, 0};
	for (const FString& Bad : {FString(TEXT("")), FString(TEXT("hello")), FString(TEXT("DC1-UUUU")), Typo, DCOutfit::Encode(TooFar), DCOutfit::Encode(BadSwatch), DCOutfit::Encode(FewerSlots), FString(TEXT("DC1-")) + FString::ChrN(200, TEXT('0'))})
	{
		FDCOutfit Out = DCOutfit::Default(T.OutfitSlots);
		Out.Accent = 7;
		FString Error;
		TestFalse(FString::Printf(TEXT("'%s' rejected"), *Bad.Left(30)), DCOutfit::Decode(Bad, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), Out, Error));
		TestTrue(TEXT("with a reason"), !Error.IsEmpty());
		TestEqual(TEXT("output untouched"), static_cast<int32>(Out.Accent), 7);
	}
	// Every worn part's model ships and parses.
	for (const FDCOutfitPart& P : T.OutfitParts)
	{
		TestTrue(FString::Printf(TEXT("part %s has its model"), *P.Id.ToString()), P.Model.IsNone() || DCVoxelModels::Parsed(P.Model) != nullptr);
	}
	return true;
}

#endif
