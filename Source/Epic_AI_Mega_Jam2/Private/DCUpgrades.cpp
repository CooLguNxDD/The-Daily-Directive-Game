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
	using FCallFn = bool (*)(ADCGameMode&, ADCPlayerState&, ADCCharacter&, const FDCUpgradeDef&, const FVector&);

	/** One card kind: what the pick does at once, and optional hooks later in the run (null = none). */
	struct FEffectHooks
	{
		FEffectFn Apply = nullptr;
		FSectorFn SectorStart = nullptr;
		FInteractFn Interact = nullptr;
		FDownedFn Downed = nullptr;
		/** Call-in list (human 15:58): the card's entry was called at a target point (spends a charge when it acts). */
		FCallFn Call = nullptr;
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

	/** R22 sentry turret: set one down while a charge is left (the host checks ground). Interact places it ahead of the
	 *  player (Aim null), the call-in list at the called point. */
	bool DeployAt(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, const FDCUpgradeDef& Def, const FVector* Aim)
	{
		int32& Left = PS.HostCardCharges.FindOrAdd(Def.Id);
		const FDCCompanionDef* Kind = UDCTuning::Get().FindCompanion(Def.Companion);
		UDCCompanions* Pool = UDCCompanions::Get(&GM);
		if (Left <= 0 || !Kind || !Pool || !Pool->HostDeploy(Player, PS, *Kind, Aim))
		{
			return false;
		}
		--Left;
		Toast(PS, Def.UseToast, Left);
		return true;
	}

	bool DeployCompanion(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, const FDCUpgradeDef& Def)
	{
		return DeployAt(GM, PS, Player, Def, nullptr);
	}

	bool CallDeploy(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, const FDCUpgradeDef& Def, const FVector& Target)
	{
		return DeployAt(GM, PS, Player, Def, &Target);
	}

	/** Gun drone on call: one charge launches one drone of the row's Companion kind (Count charges per sector). */
	bool CallCompanion(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter&, const FDCUpgradeDef& Def, const FVector&)
	{
		int32& Left = PS.HostCardCharges.FindOrAdd(Def.Id);
		const FDCCompanionDef* Kind = UDCTuning::Get().FindCompanion(Def.Companion);
		UDCCompanions* Pool = UDCCompanions::Get(&GM);
		if (Left <= 0 || !Kind || !Pool || Pool->HostLaunchOrbiters(PS, *Kind, 1) == 0)
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
			{TEXT("Companion"), {&Gear, &LaunchCompanions, nullptr, nullptr, &CallCompanion}},
			{TEXT("Deploy"), {&Gear, &DealCharges, &DeployCompanion, nullptr, &CallDeploy}},
			{TEXT("LastStand"), {&Gear, &DealCharges, nullptr, &LastStand}},
		};
		return Map;
	}

	/** Call-in modes by id (FDCUpgradeDef::CallIn): an Active card is dealt Count charges at sector start, which its list
	 *  entry spends, instead of its effect's own sector-start hook (the gun drone no longer launches by itself). None and
	 *  Status keep the effect's hook. A new mode is a new entry. */
	const TMap<FName, FSectorFn>& CallInSectorStart()
	{
		static const TMap<FName, FSectorFn> Map = {
			{TEXT("Active"), &DealCharges},
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
		if (!Def.CallIn.IsNone() && UDCTuning::Get().FindStratagem(Def.CallStratagem))
		{
			PS.OwnedStratagems.AddUnique(Def.CallStratagem); // the card's entry in the call-in list (slot 2, Q)
		}
		return true;
	}

	bool IsKnownCallIn(FName CallIn)
	{
		static const TArray<FName> Modes = {NAME_None, TEXT("Active"), TEXT("Status")};
		return Modes.Contains(CallIn);
	}

	bool CallCard(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player, FName CardId, const FVector& Target)
	{
		const FDCUpgradeDef* Def = UDCTuning::Get().FindUpgrade(CardId);
		const FEffectHooks* Hooks = Def ? Effects().Find(Def->Effect) : nullptr;
		if (!Hooks || !Hooks->Call || !PS.Upgrades.Contains(CardId) || PS.HostCardCharges.FindRef(CardId) <= 0)
		{
			UE_LOG(LogTemp, Display, TEXT("DCCALLIN %s %s refused (owned=%d left=%d)"), *PS.GetPlayerName(), *CardId.ToString(), PS.Upgrades.Contains(CardId) ? 1 : 0, PS.HostCardCharges.FindRef(CardId));
			return false;
		}
		const bool bActed = Hooks->Call(GM, PS, Player, *Def, Target);
		PS.SyncCardCharges();
		UE_LOG(LogTemp, Display, TEXT("DCCALLIN %s %s at %s acted=%d left=%d"), *PS.GetPlayerName(), *CardId.ToString(), *Target.ToCompactString(), bActed ? 1 : 0, PS.HostCardCharges.FindRef(CardId));
		return bActed;
	}

	bool IsKnownEffect(FName Effect)
	{
		return Effects().Contains(Effect);
	}

	void SectorStart(ADCGameMode& GM, ADCPlayerState& PS)
	{
		PS.HostCardCharges.Reset();
		ForOwned(PS, [&](const FEffectHooks& H, const FDCUpgradeDef& Def) {
			const FSectorFn* Override = CallInSectorStart().Find(Def.CallIn);
			const FSectorFn Fn = Override ? *Override : H.SectorStart;
			if (Fn)
			{
				Fn(GM, PS, Def);
			}
			return true;
		});
		PS.SyncCardCharges();
	}

	bool Interact(ADCGameMode& GM, ADCPlayerState& PS, ADCCharacter& Player)
	{
		bool bActed = false;
		ForOwned(PS, [&](const FEffectHooks& H, const FDCUpgradeDef& Def) {
			bActed = H.Interact && H.Interact(GM, PS, Player, Def);
			return !bActed; // the first card that acts takes the press
		});
		PS.SyncCardCharges();
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
		PS.SyncCardCharges();
	}
}
