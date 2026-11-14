// Title menu and lobby (Slate): solo, host a listen server, join by IP, quit; the host starts the run.
#pragma once

#include "CoreMinimal.h"

class ADCPlayerController;
class SWidget;
struct FDCLanHost;

namespace DCMenu
{
	/** Mode 0: main menu (not connected). 1: hosting (lobby with START). 2: joined (waiting for the host). */
	TSharedRef<SWidget> Build(ADCPlayerController* PC, uint8 Mode, const TArray<FDCLanHost>& LanHosts);
	/** The SOLO RUN button of the last built main menu (evidence runs click it through real Slate input). */
	TWeakPtr<SWidget> SoloButton();
}
