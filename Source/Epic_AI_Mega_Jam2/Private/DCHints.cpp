#include "DCHints.h"

#include "DCCharacter.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCObjective.h"
#include "DCPlayerController.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "DCUnits.h"
#include "Rules/DCFactionRules.h"

namespace DCHints
{
	namespace
	{
		using FTrigger = bool (*)(const ADCPlayerController&, const ADCGameState&, const ADCCharacter*);

		bool InSector(const ADCGameState& GS) { return GS.Phase == EDCRunPhase::Sector; }

		bool FirstEnemy(const ADCPlayerController& PC, const ADCGameState& GS, const ADCCharacter* Me)
		{
			const UDCUnits* Units = UDCUnits::Get(&PC);
			if (!InSector(GS) || !Me || !Units)
			{
				return false;
			}
			const UDCTuning& T = UDCTuning::Get();
			const float R2 = FMath::Square(T.Run.HintEnemyRangeCm);
			for (const ADCEnemy* U : Units->All())
			{
				if (U && U->IsAlive() && DCFactionRules::FightsCrew(T.Factions, U->GetFaction()) && FVector::DistSquared(U->GetActorLocation(), Me->GetActorLocation()) <= R2)
				{
					return true;
				}
			}
			return false;
		}

		bool LowAmmo(const ADCPlayerController&, const ADCGameState& GS, const ADCCharacter* Me)
		{
			return InSector(GS) && Me && !Me->IsDowned() && Me->GetAmmo() + Me->GetReserve() <= UDCTuning::Get().Run.HintLowAmmoRounds;
		}

		// The free laser is ready and the player has been in the sector a while (not the opening second).
		bool StrikeReady(const ADCPlayerController& PC, const ADCGameState& GS, const ADCCharacter* Me)
		{
			const ADCPlayerState* PS = PC.GetPlayerState<ADCPlayerState>();
			return InSector(GS) && Me && PS && PS->CooldownReadyTime(TEXT("OrbitalLaser")) <= GS.Now() && PC.GetHintSectorSeconds() >= UDCTuning::Get().Run.HintStrikeAfterSeconds;
		}

		bool Draft(const ADCPlayerController&, const ADCGameState& GS, const ADCCharacter*) { return GS.Phase == EDCRunPhase::Draft; }

		bool Downed(const ADCPlayerController&, const ADCGameState& GS, const ADCCharacter* Me) { return InSector(GS) && Me && Me->IsDowned(); }

		bool Objective(const ADCPlayerController& PC, const ADCGameState& GS, const ADCCharacter*)
		{
			const ADCObjective* O = ADCObjective::Find(&PC);
			return InSector(GS) && O && O->Stage == EDCObjectiveStage::Active;
		}

		bool Extract(const ADCPlayerController& PC, const ADCGameState& GS, const ADCCharacter*)
		{
			const ADCObjective* O = ADCObjective::Find(&PC);
			return InSector(GS) && O && O->Stage == EDCObjectiveStage::Extract;
		}

		const TMap<FName, FTrigger>& Registry()
		{
			static const TMap<FName, FTrigger> R = {
				{TEXT("FirstEnemy"), &FirstEnemy}, {TEXT("LowAmmo"), &LowAmmo}, {TEXT("StrikeReady"), &StrikeReady}, {TEXT("Draft"), &Draft},
				{TEXT("Downed"), &Downed}, {TEXT("Objective"), &Objective}, {TEXT("Extract"), &Extract},
			};
			return R;
		}
	}

	bool Holds(FName Id, const ADCPlayerController& PC)
	{
		const FTrigger* Fn = Registry().Find(Id);
		const ADCGameState* GS = PC.GetWorld() ? PC.GetWorld()->GetGameState<ADCGameState>() : nullptr;
		return Fn && GS && (*Fn)(PC, *GS, Cast<ADCCharacter>(PC.GetPawn()));
	}

	bool IsKnown(FName Id) { return Registry().Contains(Id); }
}
