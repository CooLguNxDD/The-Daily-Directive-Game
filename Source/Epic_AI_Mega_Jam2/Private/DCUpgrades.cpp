#include "DCUpgrades.h"

#include "DCCharacter.h"
#include "DCCompanion.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCPlayerState.h"
#include "DCTuning.h"

namespace
{
	using FEffectFn = void (*)(ADCGameState&, ADCPlayerState&, const FDCUpgradeDef&);
	using FSectorFn = void (*)(ADCGameMode&, ADCPlayerState&, const FDCUpgradeDef&);
	using FInteractFn = bool (*)(ADCGameMode&, ADCPlayerState&, ADCCharacter&, const FDCUpgradeDef&);
	using FDownedFn = void (*)(ADCGameMode&, ADCPlayerState&, ADCCharacter&, const FDCUpgradeDef&);

	/** One card kind: what the pick does at once, and optional hooks later in the run (null = none). */
	struct FEffectHooks
	{
		FEffectFn Apply = nullptr;
		FSectorFn SectorStart = nullptr;
		FInteractFn Interact = nullptr;
		FDownedFn Downed = nullptr;
	};

	void Toast(ADCPlayerState& PS, const FString& Template, int32 Left)
	{
		ADCCharacter* C = PS.GetPawn<ADCCharacter>();
		if (C && !Template.IsEmpty())
		{
			C->HostToast(Template.Replace(TEXT("{N}"), *FString::FromInt(Left)));
		}
	}

	/** Cinema Stratagems: grant a new stratagem; calling it still costs Propaganda Points. */
	void UnlockStratagem(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.OwnedStratagems.AddUnique(Def.Unlock);
	}

	/** Cursed Disposable: fewer shots (FilmShots), every photo worth more (PhotoMultiplier). From the next sector. */
	void Film(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
		PS.FilmPerSector = Def.FilmShots > 0 ? Def.FilmShots : PS.FilmPerSector;
		PS.PhotoMultiplier *= FMath::Max(Def.PhotoMultiplier, 0.f);
	}

	/** Strobe Flash and other shutter gear: read at the shutter from the row (StunRangeCm, StunSeconds). Also the pick
	 *  of every R22 card that acts later through its hooks. */
	void Gear(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
	}

	/** Fisheye Danger Lens: wider viewfinder (ExtraFOV degrees) and a bigger danger bonus (DangerMultiplier). */
	void Fisheye(ADCGameState&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.Gear.AddUnique(Def.Id);
		PS.DangerMultiplier *= FMath::Max(Def.DangerMultiplier, 0.f);
		PS.ExtraFOV += Def.ExtraFOV;
	}

	/** City Mutators change the whole city for everyone. */
	void Mutator(ADCGameState& GS, ADCPlayerState&, const FDCUpgradeDef& Def)
	{
		GS.Mutators.AddUnique(Def.Id);
	}

	/** R22 gun drone: Count companions of the row's Companion kind orbit the owner every sector. */
	void LaunchCompanions(ADCGameMode& GM, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		const FDCCompanionDef* Kind = UDCTuning::Get().FindCompanion(Def.Companion);
		UDCCompanions* Pool = UDCCompanions::Get(&GM);
		if (Kind && Pool)
		{
			Pool->HostLaunchOrbiters(PS, *Kind, Def.Count);
			Toast(PS, Def.ReadyToast, Def.Count);
		}
	}

	/** R22 charges (turret deploys, last stands): Count per sector. */
	void DealCharges(ADCGameMode&, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		PS.HostCardCharges.Add(Def.Id, Def.Count);
		Toast(PS, Def.ReadyToast, Def.Count);
	}

	/** R22 sentry turret: Interact sets one down ahead of the player while a charge is left (the host checks ground). */
	bool DeployCompanion(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, const FDCUpgradeDef& Def)
	{
		int32& Left = PS.HostCardCharges.FindOrAdd(Def.Id);
		const FDCCompanionDef* Kind = UDCTuning::Get().FindCompanion(Def.Companion);
		UDCCompanions* Pool = UDCCompanions::Get(&GM);
		if (Left <= 0 || !Kind || !Pool || !Pool->HostDeploy(Player, PS, *Kind))
		{
			return false;
		}
		--Left;
		Toast(PS, Def.UseToast, Left);
		return true;
	}

	/** R22 Dead Man's Switch: going down calls the row's Unlock stratagem on the spot, free, while a charge is left. */
	void LastStand(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, const FDCUpgradeDef& Def)
	{
		int32& Left = PS.HostCardCharges.FindOrAdd(Def.Id);
		const FDCStratagemDef* Strike = UDCTuning::Get().FindStratagem(Def.Unlock);
		if (Left <= 0 || !Strike)
		{
			return;
		}
		--Left;
		GM.CallStrike(*Strike, Player.GetActorLocation(), Player.GetController());
		Toast(PS, Def.UseToast, Left);
		UE_LOG(LogTemp, Display, TEXT("DCLASTSTAND %s called %s on the spot (%d left)"), *PS.GetPlayerName(), *Strike->Id.ToString(), Left);
	}

	const TMap<FName, FEffectHooks>& Effects()
	{
		static const TMap<FName, FEffectHooks> Map = {
			{TEXT("UnlockStratagem"), {&UnlockStratagem}},
			{TEXT("Film"), {&Film}},
			{TEXT("Gear"), {&Gear}},
			{TEXT("Fisheye"), {&Fisheye}},
			{TEXT("Mutator"), {&Mutator}},
			{TEXT("Companion"), {&Gear, &LaunchCompanions}},
			{TEXT("Deploy"), {&Gear, &DealCharges, &DeployCompanion}},
			{TEXT("LastStand"), {&Gear, &DealCharges, nullptr, &LastStand}},
		};
		return Map;
	}

	/** Every owned card's row with its hooks, in pick order. */
	template <typename FnT>
	void ForOwned(const ADCPlayerState& PS, FnT&& Fn)
	{
		const UDCTuning& T = UDCTuning::Get();
		for (const FName& Id : PS.Upgrades)
		{
			const FDCUpgradeDef* Def = T.FindUpgrade(Id);
			const FEffectHooks* Hooks = Def ? Effects().Find(Def->Effect) : nullptr;
			if (Hooks && !Fn(*Hooks, *Def))
			{
				return;
			}
		}
	}
}

namespace DCUpgrades
{
	bool Apply(ADCGameState& GS, ADCPlayerState& PS, const FDCUpgradeDef& Def)
	{
		const FEffectHooks* Hooks = Effects().Find(Def.Effect);
		if (!Hooks)
		{
			UE_LOG(LogTemp, Warning, TEXT("DCUpgrades: unknown effect %s on %s"), *Def.Effect.ToString(), *Def.Id.ToString());
			return false;
		}
		Hooks->Apply(GS, PS, Def);
		return true;
	}

	bool IsKnownEffect(FName Effect)
	{
		return Effects().Contains(Effect);
	}

	void SectorStart(ADCGameMode& GM, ADCPlayerState& PS)
	{
		PS.HostCardCharges.Reset();
		ForOwned(PS, [&](const FEffectHooks& H, const FDCUpgradeDef& Def) {
			if (H.SectorStart)
			{
				H.SectorStart(GM, PS, Def);
			}
			return true;
		});
	}

	bool Interact(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player)
	{
		bool bActed = false;
		ForOwned(PS, [&](const FEffectHooks& H, const FDCUpgradeDef& Def) {
			bActed = H.Interact && H.Interact(GM, PS, Player, Def);
			return !bActed; // the first card that acts takes the press
		});
		return bActed;
	}

	void Downed(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player)
	{
		ForOwned(PS, [&](const FEffectHooks& H, const FDCUpgradeDef& Def) {
			if (H.Downed)
			{
				H.Downed(GM, PS, Player, Def);
			}
			return true;
		});
	}
}
