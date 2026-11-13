#include "DCUpgrades.h"

#include "DCGameState.h"
#include "DCPlayerState.h"
#include "DCTuning.h"

namespace
{
	using FEffectFn = void (*)(ADCGameState&, ADCPlayerState&, const FDCUpgradeDef&);

	/** Cinema Stratagems: grant a new stratagem; calling it still costs Propaganda Points. */
	void UnlockStratagem(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.OwnedStratagems.AddUnique(Def.Unlock);
	}

	/** Cursed Disposable: fewer shots (Param), every photo worth more (Param2). Takes effect from the next sector. */
	void Film(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
		PS.FilmPerSector = FMath::Max(1, FMath::RoundToInt(Def.Param));
		PS.PhotoMultiplier *= FMath::Max(Def.Param2, 0.f);
	}

	/** Strobe Flash and other shutter gear: read at the shutter by id (range Param cm, Param2 seconds). */
	void Gear(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
	}

	/** Fisheye Danger Lens: wider viewfinder (Param2 degrees) and a bigger danger bonus (Param x). */
	void Fisheye(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
		PS.DangerMultiplier *= FMath::Max(Def.Param, 0.f);
		PS.ExtraFOV += Def.Param2;
	}

	/** City Mutators change the whole city for everyone. */
	void Mutator(ADCGameState& GS, ADCPlayerState&, const FDCUpgradeDef& Def)
	{
		GS.Mutators.AddUnique(Def.Id);
	}

	const TMap<FName, FEffectFn>& Effects()
	{
		static const TMap<FName, FEffectFn> Map = {
			{TEXT("UnlockStratagem"), &UnlockStratagem},
			{TEXT("Film"), &Film},
			{TEXT("Gear"), &Gear},
			{TEXT("Fisheye"), &Fisheye},
			{TEXT("Mutator"), &Mutator},
		};
		return Map;
	}
}

namespace DCUpgrades
{
	bool Apply(ADCGameState& GS, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		const FEffectFn* Fn = Effects().Find(Def.Effect);
		if (!Fn)
		{
			UE_LOG(LogTemp, Warning, TEXT("DCUpgrades: unknown effect %s on %s"), *Def.Effect.ToString(), *Def.Id.ToString());
			return false;
		}
		(*Fn)(GS, PS, Def);
		return true;
	}

	bool IsKnownEffect(FName Effect)
	{
		return Effects().Contains(Effect);
	}
}
