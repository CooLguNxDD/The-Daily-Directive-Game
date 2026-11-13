// Enhanced Input actions and the mapping context, built at run time from the binding rows in UDCTuning
// (Config/DefaultGame.ini). Adding a control means adding rows, not assets.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DCInput.generated.h"

class UInputAction;
class UInputMappingContext;
class AController;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCInputSet : public UObject
{
	GENERATED_BODY()

public:
	/** The set owned by this controller's ADCPlayerController (built on first use). */
	static UDCInputSet* Get(AController* Controller);

	void Build();
	const UInputAction* Find(FName Action) const;
	UInputMappingContext* GetContext() const { return Context; }

private:
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
};
