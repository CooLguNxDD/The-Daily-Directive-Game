// The UI palette shared by the HUD, the menu and the front page (one owner for UI colours). R25 (human 2026-11-13):
// restrained neon noir. The role names stay (Paper = panel ground, Ink = type and rules) so every screen follows.
#pragma once

#include "CoreMinimal.h"

namespace DCPalette
{
	// Linear values of the art bible hex colours (the HUD converts to sRGB).
	// Panel ground: dark glass #0F121C. Type and rules: cool light #E6E9F2.
	inline const FLinearColor Paper(0.005f, 0.006f, 0.012f, 1.f);
	inline const FLinearColor Ink(0.791f, 0.815f, 0.887f, 1.f);
	// Danger red #FF2A3A (danger only), signal lime-yellow #E8FF3A (usable things only).
	inline const FLinearColor Red(1.f, 0.023f, 0.042f, 1.f);
	inline const FLinearColor Gold(0.807f, 1.f, 0.042f, 1.f);
	// Neon accents, sparing: cyan #2BC4D8 for panel edges and the player's things, magenta #E0306F for headlines.
	inline const FLinearColor Cyan(0.024f, 0.552f, 0.687f, 1.f);
	inline const FLinearColor Magenta(0.745f, 0.030f, 0.159f, 1.f);
	// Night #0A0D18: type on a bright plate (lime, red, cyan).
	inline const FLinearColor Night(0.003f, 0.004f, 0.009f, 1.f);
	inline const FLinearColor Shade(0.f, 0.f, 0.f, 0.55f);
	inline const FLinearColor Dim(0.f, 0.f, 0.f, 0.72f);

	/** Type that reads on a fill: night on bright plates, light type on dark glass. */
	inline FLinearColor TextOn(const FLinearColor& Fill)
	{
		return Fill.GetLuminance() > 0.2f ? Night : Ink;
	}
}
