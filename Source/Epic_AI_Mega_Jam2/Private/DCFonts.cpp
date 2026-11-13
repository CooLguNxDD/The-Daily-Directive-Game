#include "DCFonts.h"

#include "DCTuning.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Styling/CoreStyle.h"

namespace DCFonts
{
	namespace
	{
		TMap<FName, TObjectPtr<UFont>>& Cache()
		{
			static TMap<FName, TObjectPtr<UFont>> Fonts; // rooted below: they live as long as the game
			return Fonts;
		}
	}

	UFont* Get(FName Role)
	{
		if (const TObjectPtr<UFont>* Found = Cache().Find(Role))
		{
			return *Found;
		}
		const FSoftObjectPath* Path = UDCTuning::Get().Assets.FontFaces.Find(Role);
		UFontFace* Face = Path ? Cast<UFontFace>(Path->TryLoad()) : nullptr;
		if (!Face)
		{
			return GEngine ? GEngine->GetLargeFont() : nullptr;
		}
		// A runtime font over the one face: the canvas rasterises glyphs at the drawn size (crisp at any scale).
		UFont* Font = NewObject<UFont>(GetTransientPackage(), *FString::Printf(TEXT("DCFont_%s"), *Role.ToString()));
		Font->FontCacheType = EFontCacheType::Runtime;
		const int32 Base = UDCTuning::Get().Assets.FontBaseSize;
		const UFont* Engine = GEngine ? GEngine->GetLargeFont() : nullptr;
		Font->LegacyFontSize = Base > 0 ? Base : (Engine ? Engine->LegacyFontSize : 24);
		UE_LOG(LogTemp, Display, TEXT("DCFONT %s face=%s size=%d"), *Role.ToString(), *Face->GetName(), Font->LegacyFontSize);
		FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(Face);
		Font->AddToRoot();
		Cache().Add(Role, Font);
		return Font;
	}

	FSlateFontInfo Slate(FName Role, float Size)
	{
		UFont* Font = Get(Role);
		if (!Font || Font->FontCacheType != EFontCacheType::Runtime)
		{
			return FCoreStyle::GetDefaultFontStyle("Bold", Size);
		}
		return FSlateFontInfo(Font, Size);
	}
}
