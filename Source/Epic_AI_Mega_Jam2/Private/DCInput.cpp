#include "DCInput.h"
#include "DCSave.h"

#include "DCPlayerController.h"
#include "DCTuning.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	const EInputActionValueType AxisTypes[] = {EInputActionValueType::Boolean, EInputActionValueType::Axis1D, EInputActionValueType::Axis2D};
}

UDCInputSet* UDCInputSet::Get(AController* Controller)
{
	ADCPlayerController* PC = Cast<ADCPlayerController>(Controller);
	return PC ? PC->GetInputSet() : nullptr;
}

void UDCInputSet::Build()
{
	const UDCTuning& T = UDCTuning::Get();
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_DirectorsCut"));
	for (const FDCActionDef& Def : T.Actions)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Def.Action);
		Action->ValueType = AxisTypes[FMath::Clamp(Def.Axes, 0, 2)];
		Actions.Add(Def.Action, Action);
	}
	// The player's rebinds (R24, settings.json): an action's first binding row takes the saved key.
	TSet<FName> Rebound;
	const UDCSaveSubsystem* Save = UDCSaveSubsystem::Get(GetOuter());
	for (FDCKeyBinding Row : T.Bindings)
	{
		const FString* Saved = Save ? Save->Settings.KeyBindings.Find(Row.Action.ToString()) : nullptr;
		if (Saved && !Rebound.Contains(Row.Action) && FKey(**Saved).IsValid())
		{
			Row.Key = FKey(**Saved);
			Rebound.Add(Row.Action);
		}
		const TObjectPtr<UInputAction>* Action = Actions.Find(Row.Action);
		if (!Action || !Row.Key.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("DCInput: skipped binding %s -> %s"), *Row.Action.ToString(), *Row.Key.ToString());
			continue;
		}
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(*Action, Row.Key);
		if (Row.bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (Row.bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}
}

const UInputAction* UDCInputSet::Find(FName Action) const
{
	const TObjectPtr<UInputAction>* Found = Actions.Find(Action);
	return Found ? Found->Get() : nullptr;
}
