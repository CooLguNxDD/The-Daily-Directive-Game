// City builder preview (R16): draws every sector's plan from a run seed as a PNG, no rendering needed.
//   UnrealEditor-Cmd.exe <project> -run=DCCityPreview -seed=4242 -out=<dir> [-sectors=3]
// The rendered sector comes from a game run: -game -dccityshot=<dir> -dcseed=<seed> (aerial frames, then quits).
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DCCityPreviewCommandlet.generated.h"

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCCityPreviewCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;

	/** Plan image: streets, paved lots, towers shaded by height (landmark outlined), plazas, rubble zones, props, and
	 *  the sight line from the spawn street. Pure apart from the file write. */
	static bool WritePlanPng(const struct FDCCityPlan& Plan, const FString& Path);
};
