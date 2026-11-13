// Print type by role (R7): the OFL faces imported by tools/content/build_content.py, looked up by role name from the
// asset tuning (Masthead, Headline, Body, Stamp). One owner for which face sets which text.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UFont;

namespace DCFonts
{
	/** A runtime-cached font for the role; the engine's large font when the role or its face is missing. */
	UFont* Get(FName Role);
	/** The role's face for Slate widgets (menu) at a point size. */
	FSlateFontInfo Slate(FName Role, float Size);
}
