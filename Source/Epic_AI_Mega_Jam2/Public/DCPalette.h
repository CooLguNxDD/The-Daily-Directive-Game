// The propaganda print palette shared by the HUD, the menu and the front page (one owner for UI colours).
#pragma once

#include "CoreMinimal.h"

namespace DCPalette
{
	// Linear values of the art bible hex colours (the HUD converts to sRGB): ink #16161A, paper #F2E8D0,
	// propaganda red #D8261C (danger only), signal yellow #FFC61A (usable things only).
	inline const FLinearColor Ink(0.008f, 0.008f, 0.010f, 1.f);
	inline const FLinearColor Paper(0.888f, 0.807f, 0.631f, 1.f);
	inline const FLinearColor Red(0.687f, 0.019f, 0.011f, 1.f);
	inline const FLinearColor Gold(1.f, 0.565f, 0.010f, 1.f);
	inline const FLinearColor Shade(0.f, 0.f, 0.f, 0.55f);
	inline const FLinearColor Dim(0.f, 0.f, 0.f, 0.72f);
}
