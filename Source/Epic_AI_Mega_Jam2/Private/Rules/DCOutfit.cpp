#include "Rules/DCOutfit.h"

namespace
{
	// Crockford base32 (no I, L, O, U): readable when typed from a friend's screen.
	const TCHAR* Alphabet = TEXT("0123456789ABCDEFGHJKMNPQRSTVWXYZ");
	const TCHAR* Prefix = TEXT("DC1-");

	int32 ValueOf(TCHAR C)
	{
		const TCHAR U = FChar::ToUpper(C);
		const TCHAR* Found = FCString::Strchr(Alphabet, U);
		return Found ? static_cast<int32>(Found - Alphabet) : INDEX_NONE;
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
		// Bytes: count, parts..., main, accent; each byte as two base32 digits (5 bits each, values < 32 per digit pair
		// cover 0..1023, so any byte fits); then one check digit (sum of all digit values mod 32).
		TArray<uint8> Bytes;
		Bytes.Add(static_cast<uint8>(Outfit.Parts.Num()));
		Bytes.Append(Outfit.Parts);
		Bytes.Add(Outfit.Main);
		Bytes.Add(Outfit.Accent);
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
		FString Out = Prefix;
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

	bool Decode(const FString& Code, const TArray<FName>& Slots, const TArray<FDCOutfitPart>& Parts, int32 SwatchCount, FDCOutfit& Out, FString& Error)
	{
		FString Text = Code.TrimStartAndEnd().ToUpper();
		if (!Text.StartsWith(Prefix) || Text.Len() > 64)
		{
			Error = TEXT("not a DC1 share code");
			return false;
		}
		Text.RightChopInline(FCString::Strlen(Prefix));
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
		if (Bytes.Num() < 3 || Bytes[0] != Slots.Num() || Bytes.Num() != Slots.Num() + 3)
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
		Result.Main = Bytes[1 + Slots.Num()];
		Result.Accent = Bytes[2 + Slots.Num()];
		if (Result.Main >= SwatchCount || Result.Accent >= SwatchCount)
		{
			Error = TEXT("no such swatch");
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
		O.Accent = 1;
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
			Outfit.Parts[SlotIndex] = static_cast<uint8>(((Outfit.Parts[SlotIndex] + Step) % Count + Count) % Count);
		}
	}
}
