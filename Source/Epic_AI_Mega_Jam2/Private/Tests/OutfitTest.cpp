// Photographer outfits (R23, R18): share codes round-trip (old DC1 codes still load), untrusted codes are rejected with a reason, every shipped part
// has a model that loads, and cycling wraps. Pure (the model check reads the shipped .dcvox files).
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCOutfit.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

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
			O.Glow = DCOutfit::Wrap(O.Glow, 1, T.OutfitGlows.Num());
			FDCOutfit Back;
			FString Error;
			const FString Code = DCOutfit::Encode(O);
			TestTrue(FString::Printf(TEXT("%s decodes (%s)"), *Code, *Error), DCOutfit::Decode(Code, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Back, Error));
			TestTrue(FString::Printf(TEXT("%s round-trips"), *Code), Back == O);
			TestTrue(TEXT("lower case and spaces still load"), DCOutfit::Decode(TEXT("  ") + Code.ToLower() + TEXT(" "), T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Back, Error));
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
	FDCOutfit BadGlow = DCOutfit::Default(T.OutfitSlots);
	BadGlow.Glow = 240;
	// A DC1 body read as DC2 (and the other way round) is one byte short or long: rejected, never misread.
	const FString OldAsNew = FString(TEXT("DC2-04010-20000-0503F"));
	const FString NewAsOld = FString(TEXT("DC1-")) + DCOutfit::Encode(DCOutfit::Default(T.OutfitSlots)).RightChop(4);
	for (const FString& Bad : {FString(TEXT("")), FString(TEXT("hello")), FString(TEXT("DC1-UUUU")), FString(TEXT("DC9-04010-20000-0503F")), Typo, DCOutfit::Encode(TooFar), DCOutfit::Encode(BadSwatch), DCOutfit::Encode(BadGlow), DCOutfit::Encode(FewerSlots), OldAsNew, NewAsOld, FString(TEXT("DC1-")) + FString::ChrN(200, TEXT('0'))})
	{
		FDCOutfit Out = DCOutfit::Default(T.OutfitSlots);
		Out.Accent = 7;
		FString Error;
		TestFalse(FString::Printf(TEXT("'%s' rejected"), *Bad.Left(30)), DCOutfit::Decode(Bad, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Out, Error));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOutfitOldCodeTest, "Epic_AI_Mega_Jam2.Outfit.OldCodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOutfitOldCodeTest::RunTest(const FString&)
{
	// R18: share codes made before the richer builder (R23, DC1: no glow) still load the very outfit they were made
	// for. Checked by part and swatch ids, so a reordered or inserted data row fails here, not in a friend's hands.
	const UDCTuning& T = UDCTuning::Get();
	struct FOldCode
	{
		const TCHAR* Code;
		TArray<FName> Parts;
		FName Main;
		FName Accent;
	};
	const FOldCode Cases[] = {
		{TEXT("DC1-04010-20000-0503F"), {TEXT("Helmet"), TEXT("None"), TEXT("Backpack"), TEXT("PressCam")}, TEXT("Trim"), TEXT("InkBrown")}, // the R5 card code
		{TEXT("DC1-04010-10000-02008"), {TEXT("Helmet"), TEXT("Trench"), TEXT("Backpack"), TEXT("PressCam")}, TEXT("InkBlue"), TEXT("Ink")}, // R23 builder flow frame 03
		{TEXT(" dc1-04000-00000-00026 "), {TEXT("Cap"), TEXT("Vest"), TEXT("Backpack"), TEXT("PressCam")}, TEXT("Ink"), TEXT("InkBlue")}, // the old default, typed loosely
	};
	for (const FOldCode& C : Cases)
	{
		FDCOutfit O;
		FString Error;
		const bool bLoaded = DCOutfit::Decode(C.Code, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), O, Error);
		TestTrue(FString::Printf(TEXT("%s loads (%s)"), C.Code, *Error), bLoaded);
		if (!bLoaded)
		{
			continue;
		}
		for (int32 s = 0; s < T.OutfitSlots.Num(); ++s)
		{
			const TArray<const FDCOutfitPart*> Parts = DCOutfit::PartsOf(T.OutfitSlots[s], T.OutfitParts);
			TestEqual(FString::Printf(TEXT("%s %s part"), C.Code, *T.OutfitSlots[s].ToString()), Parts[O.Parts[s]]->Id, C.Parts[s]);
		}
		TestEqual(FString::Printf(TEXT("%s main"), C.Code), T.OutfitSwatches[O.Main].Id, C.Main);
		TestEqual(FString::Printf(TEXT("%s accent"), C.Code), T.OutfitSwatches[O.Accent].Id, C.Accent);
		TestEqual(FString::Printf(TEXT("%s glow (none before R18)"), C.Code), static_cast<int32>(O.Glow), 0);
		// Saving it again writes the newest version, which loads back to the same outfit.
		FDCOutfit Again;
		TestTrue(TEXT("re-encoded as DC2"), DCOutfit::Encode(O).StartsWith(TEXT("DC2-")));
		TestTrue(TEXT("re-encoded loads"), DCOutfit::Decode(DCOutfit::Encode(O), T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Again, Error) && Again == O);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCOutfitRichTest, "Epic_AI_Mega_Jam2.Outfit.RichBuilder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCOutfitRichTest::RunTest(const FString&)
{
	// R18: at least five parts per slot including the named new ones, more swatches, glows on glowing materials only,
	// every preset resolves, and random outfits are always valid codes.
	const UDCTuning& T = UDCTuning::Get();
	for (const FName& Slot : T.OutfitSlots)
	{
		TestTrue(FString::Printf(TEXT("%s has 5+ parts"), *Slot.ToString()), DCOutfit::PartsOf(Slot, T.OutfitParts).Num() >= 5);
	}
	struct FWanted
	{
		const TCHAR* Key;
		TArray<const TCHAR*> Value;
	};
	const FWanted Wanted[] = {
		{TEXT("Head"), {TEXT("Goggles"), TEXT("GasMask"), TEXT("Fedora"), TEXT("Hood"), TEXT("CyberVisor")}},
		{TEXT("Body"), {TEXT("Hoodie"), TEXT("Armour"), TEXT("Poncho"), TEXT("Bomber")}},
		{TEXT("Gear"), {TEXT("Drone"), TEXT("Satchel"), TEXT("Radio"), TEXT("Tripod")}},
		{TEXT("Camera"), {TEXT("DroneCam"), TEXT("CineCam"), TEXT("InstantCam")}},
	};
	for (const auto& W : Wanted)
	{
		const TArray<const FDCOutfitPart*> Parts = DCOutfit::PartsOf(W.Key, T.OutfitParts);
		for (const TCHAR* Id : W.Value)
		{
			const FDCOutfitPart* const* Found = Parts.FindByPredicate([Id](const FDCOutfitPart* P) { return P->Id == FName(Id); });
			TestTrue(FString::Printf(TEXT("%s part %s ships with a voxel-forge model"), W.Key, Id), Found && !(*Found)->Model.IsNone() && DCVoxelModels::Parsed((*Found)->Model) != nullptr);
		}
	}
	TestTrue(TEXT("more swatches than the R23 six"), T.OutfitSwatches.Num() > 6);
	TestTrue(TEXT("a few glow colours"), T.OutfitGlows.Num() >= 3);
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TestTrue(TEXT("SwatchGlow palette slot ships"), V.Palette.ContainsByPredicate([](const FDCVoxelMaterial& M) { return M.Id == TEXT("SwatchGlow"); }));
	for (const FDCOutfitSwatch& G : T.OutfitGlows)
	{
		TestTrue(FString::Printf(TEXT("glow %s is a palette material"), *G.Id.ToString()), V.Palette.ContainsByPredicate([&G](const FDCVoxelMaterial& P) { return P.Id == G.Material; }));
		TestTrue(FString::Printf(TEXT("glow %s is not threat red"), *G.Id.ToString()), G.Material != TEXT("Danger"));
	}
	for (const FDCOutfitSwatch& Sw : T.OutfitSwatches)
	{
		TestTrue(FString::Printf(TEXT("swatch %s is a palette material"), *Sw.Id.ToString()), V.Palette.ContainsByPredicate([&Sw](const FDCVoxelMaterial& P) { return P.Id == Sw.Material; }));
	}
	TestTrue(TEXT("a few presets"), T.OutfitPresets.Num() >= 3);
	for (const FDCOutfitPreset& P : T.OutfitPresets)
	{
		FDCOutfit O;
		FString Error;
		TestTrue(FString::Printf(TEXT("preset %s resolves (%s)"), *P.Id.ToString(), *Error), DCOutfit::FromPreset(P, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches, T.OutfitGlows, O, Error));
		FDCOutfit Back;
		TestTrue(FString::Printf(TEXT("preset %s round-trips its code"), *P.Id.ToString()), DCOutfit::Decode(DCOutfit::Encode(O), T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Back, Error) && Back == O);
	}
	FDCOutfitPreset Broken;
	Broken.Id = TEXT("Broken");
	Broken.Parts = {TEXT("NoSuchHat"), TEXT("Vest"), TEXT("Backpack"), TEXT("PressCam")};
	FDCOutfit Untouched = DCOutfit::Default(T.OutfitSlots);
	FString Why;
	TestFalse(TEXT("a preset naming a missing part is refused"), DCOutfit::FromPreset(Broken, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches, T.OutfitGlows, Untouched, Why));
	TestTrue(TEXT("with a reason"), !Why.IsEmpty());
	FRandomStream Rng(1234);
	for (int32 i = 0; i < 200; ++i)
	{
		const FDCOutfit R = DCOutfit::Random(Rng, T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num());
		FDCOutfit Back;
		FString Error;
		TestTrue(TEXT("random outfit is a valid code"), DCOutfit::Decode(DCOutfit::Encode(R), T.OutfitSlots, T.OutfitParts, T.OutfitSwatches.Num(), T.OutfitGlows.Num(), Back, Error) && Back == R);
		TestTrue(TEXT("random main and accent differ"), R.Main != R.Accent);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCVoxelBodyTest, "Epic_AI_Mega_Jam2.Outfit.VoxelBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDCVoxelBodyTest::RunTest(const FString&)
{
	// R10/R23 (advisor CP14): the photographer is built from voxel segments on the skeleton: head, torso, both arms
	// and both legs are shipped rows, and every row's model is a forged voxel model that parses.
	const UDCTuning& T = UDCTuning::Get();
	TSet<FName> Bones;
	for (const FDCBodySegment& S : T.BodySegments)
	{
		Bones.Add(S.Bone);
		TestNotNull(FString::Printf(TEXT("segment %s model %s parses"), *S.Bone.ToString(), *S.Model.ToString()), DCVoxelModels::Parsed(S.Model));
		const FDCVoxelModel* M = DCVoxelModels::Parsed(S.Model);
		TestTrue(FString::Printf(TEXT("segment %s starts at its bone (pivot x 0)"), *S.Bone.ToString()), !M || S.Aim.IsNone() || M->Pivot.X == 0);
	}
	for (const TCHAR* Need : {TEXT("pelvis"), TEXT("spine_02"), TEXT("head"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r")})
	{
		TestTrue(FString::Printf(TEXT("body covers %s"), Need), Bones.Contains(FName(Need)));
	}
	return true;
}

#endif
