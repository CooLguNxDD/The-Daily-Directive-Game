#include "Rules/DCOutfit.h"

namespace
{
	// Crockford base32 (no I, L, O, U): readable when typed from a friend's screen.
	const TCHAR* Alphabet = TEXT("0123456789ABCDEFGHJKMNPQRSTVWXYZ");

	/** A share-code version (R18): its prefix and how many bytes follow the part indices. The tail fills main, accent,
	 *  glow in that order; bytes a version does not carry keep the defaults below. Newest last (Encode writes it).
	 *  A new field is a new row with one more tail byte; old rows stay so old codes keep loading. */
	struct FCodeVersion
	{
		const TCHAR* Prefix;
		int32 Tail;
	};
	const FCodeVersion Versions[] = {
		{TEXT("DC1-"), 2}, // R23: main, accent
		{TEXT("DC2-"), 3}, // R18: + glow
	};
	constexpr int32 MaxTail = 3;

	const FCodeVersion* VersionOf(const FString& Text)
	{
		for (const FCodeVersion& V : Versions)
		{
			if (Text.StartsWith(V.Prefix))
			{
				return &V;
			}
		}
		return nullptr;
	}

	int32 ValueOf(TCHAR C)
	{
		const TCHAR U = FChar::ToUpper(C);
		const TCHAR* Found = FCString::Strchr(Alphabet, U);
		return Found ? static_cast<int32>(Found - Alphabet) : INDEX_NONE;
	}

	int32 IndexOfId(const TArray<FDCOutfitSwatch>& Rows, FName Id)
	{
		return Rows.IndexOfByPredicate([Id](const FDCOutfitSwatch& S) { return S.Id == Id; });
	}
}

namespace DCOutfit
{
	TArray<const FDCOutfitPart*> PartsOf(FName Slot, const TArray<FDCOutfitPart>& Parts)
	{
		TArray<const FDCOutfitPart*> Out;
		for (const FDCOutfitPart& P : Parts)
		{
			if (P.Slot == Slot)
			{
				Out.Add(&P);
			}
		}
		return Out;
	}

	FString Encode(const FDCOutfit& Outfit)
	{
		// Bytes: count, parts..., then the newest version's tail (main, accent, glow); each byte as two base32 digits
		// (5 bits each, values < 32 per digit pair cover 0..1023, so any byte fits); then one check digit (sum of all
		// digit values mod 32).
		const FCodeVersion& Newest = Versions[UE_ARRAY_COUNT(Versions) - 1];
		const uint8 Tail[MaxTail] = {Outfit.Main, Outfit.Accent, Outfit.Glow};
		TArray<uint8> Bytes;
		Bytes.Add(static_cast<uint8>(Outfit.Parts.Num()));
		Bytes.Append(Outfit.Parts);
		Bytes.Append(Tail, Newest.Tail);
		FString Body;
		int32 Sum = 0;
		for (const uint8 B : Bytes)
		{
			for (const int32 V : {B >> 5, B & 31})
			{
				Body.AppendChar(Alphabet[V]);
				Sum += V;
			}
		}
		Body.AppendChar(Alphabet[Sum % 32]);
		// Dashes every five characters keep it readable aloud.
		FString Out = Newest.Prefix;
		for (int32 i = 0; i < Body.Len(); ++i)
		{
			Out.AppendChar(Body[i]);
			if (i % 5 == 4 && i + 1 < Body.Len())
			{
				Out.AppendChar(TEXT('-'));
			}
		}
		return Out;
	}

	bool Decode(const FString& Code, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, int32 GlowCount, FDCOutfit& Out, FString& Error)
	{
		FString Text = Code.TrimStartAndEnd().ToUpper();
		const FCodeVersion* Version = VersionOf(Text);
		if (!Version || Text.Len() > 64)
		{
			Error = TEXT("not a DC share code");
			return false;
		}
		Text.RightChopInline(FCString::Strlen(Version->Prefix));
		Text.ReplaceInline(TEXT("-"), TEXT(""));
		TArray<int32> Values;
		for (const TCHAR C : Text)
		{
			const int32 V = ValueOf(C);
			if (V == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("bad character '%c'"), C);
				return false;
			}
			Values.Add(V);
		}
		if (Values.Num() < 3 || Values.Num() % 2 == 0)
		{
			Error = TEXT("wrong length");
			return false;
		}
		int32 Sum = 0;
		for (int32 i = 0; i + 1 < Values.Num(); ++i)
		{
			Sum += Values[i];
		}
		if (Sum % 32 != Values.Last())
		{
			Error = TEXT("check character does not match (typo?)");
			return false;
		}
		TArray<uint8> Bytes;
		for (int32 i = 0; i + 1 < Values.Num(); i += 2)
		{
			const int32 B = (Values[i] << 5) | Values[i + 1];
			if (B > 255)
			{
				Error = TEXT("value out of range");
				return false;
			}
			Bytes.Add(static_cast<uint8>(B));
		}
		if (Bytes.Num() < 1 + Version->Tail || Bytes[0] != Slots.Num() || Bytes.Num() != 1 + Slots.Num() + Version->Tail)
		{
			Error = TEXT("made for a different set of slots");
			return false;
		}
		FDCOutfit Result;
		for (int32 s = 0; s < Slots.Num(); ++s)
		{
			const uint8 Index = Bytes[1 + s];
			if (Index >= PartsOf(Slots[s], Parts).Num())
			{
				Error = FString::Printf(TEXT("no part %d for %s"), Index, *Slots[s].ToString());
				return false;
			}
			Result.Parts.Add(Index);
		}
		uint8 Tail[MaxTail] = {Result.Main, Result.Accent, 0};
		FMemory::Memcpy(Tail, &Bytes[1 + Slots.Num()], Version->Tail);
		Result.Main = Tail[0];
		Result.Accent = Tail[1];
		Result.Glow = Tail[2];
		if (Result.Main >= SwatchCount || Result.Accent >= SwatchCount)
		{
			Error = TEXT("no such swatch");
			return false;
		}
		if (Result.Glow >= FMath::Max(GlowCount, 1))
		{
			Error = TEXT("no such glow");
			return false;
		}
		Out = Result;
		return true;
	}

	FDCOutfit Default(const TArray<FName>& Slots)
	{
		FDCOutfit O;
		O.Parts.Init(0, Slots.Num());
		O.Main = 0;
		O.Accent = 2; // InkBlue: the default photographer is a dark silhouette (critic V20.3), magenta stays a choice
		O.Glow = 0;
		return O;
	}

	void Cycle(FDCOutfit& Outfit, int32 SlotIndex, int32 Step, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts)
	{
		if (!Slots.IsValidIndex(SlotIndex) || !Outfit.Parts.IsValidIndex(SlotIndex))
		{
			return;
		}
		const int32 Count = PartsOf(Slots[SlotIndex], Parts).Num();
		if (Count > 0)
		{
			Outfit.Parts[SlotIndex] = Wrap(Outfit.Parts[SlotIndex], Step, Count);
		}
	}

	uint8 Wrap(int32 Index, int32 Step, int32 Count)
	{
		const int32 N = FMath::Clamp(Count, 1, 256);
		return static_cast<uint8>(((Index + Step) % N + N) % N);
	}

	bool FromPreset(const FDCOutfitPreset& Preset, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, const TArray<FDCOutfitSwatch>& Swatches, const TArray<FDCOutfitSwatch>& Glows, FDCOutfit& Out, FString& Error)
	{
		if (Preset.Parts.Num() != Slots.Num())
		{
			Error = FString::Printf(TEXT("preset %s names %d parts for %d slots"), *Preset.Id.ToString(), Preset.Parts.Num(), Slots.Num());
			return false;
		}
		FDCOutfit Result;
		for (int32 s = 0; s < Slots.Num(); ++s)
		{
			const TArray<const FDCOutfitPart*> InSlot = PartsOf(Slots[s], Parts);
			const FName Want = Preset.Parts[s];
			const int32 Index = InSlot.IndexOfByPredicate([Want](const FDCOutfitPart* P) { return P->Id == Want; });
			if (Index == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("preset %s: no %s part %s"), *Preset.Id.ToString(), *Slots[s].ToString(), *Want.ToString());
				return false;
			}
			Result.Parts.Add(static_cast<uint8>(Index));
		}
		const int32 Main = IndexOfId(Swatches, Preset.Main);
		const int32 Accent = IndexOfId(Swatches, Preset.Accent);
		const int32 Glow = IndexOfId(Glows, Preset.Glow);
		if (Main == INDEX_NONE || Accent == INDEX_NONE || Glow == INDEX_NONE)
		{
			Error = FString::Printf(TEXT("preset %s: unknown swatch or glow"), *Preset.Id.ToString());
			return false;
		}
		Result.Main = static_cast<uint8>(Main);
		Result.Accent = static_cast<uint8>(Accent);
		Result.Glow = static_cast<uint8>(Glow);
		Out = Result;
		return true;
	}

	FDCOutfit Random(FRandomStream& Rng, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, int32 GlowCount)
	{
		FDCOutfit O = Default(Slots);
		for (int32 s = 0; s < Slots.Num(); ++s)
		{
			const int32 Count = PartsOf(Slots[s], Parts).Num();
			O.Parts[s] = static_cast<uint8>(Count > 0 ? Rng.RandRange(0, Count - 1) : 0);
		}
		const int32 Swatches = FMath::Max(SwatchCount, 1);
		O.Main = static_cast<uint8>(Rng.RandRange(0, Swatches - 1));
		// Accent: any other swatch (a step of 1..N-1 from main), so the two never match when there is a choice.
		O.Accent = Swatches > 1 ? Wrap(O.Main, Rng.RandRange(1, Swatches - 1), Swatches) : O.Main;
		O.Glow = static_cast<uint8>(Rng.RandRange(0, FMath::Max(GlowCount, 1) - 1));
		return O;
	}
}
