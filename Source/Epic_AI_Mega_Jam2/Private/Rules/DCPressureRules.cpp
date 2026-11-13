#include "Rules/DCPressureRules.h"

namespace DCPressureRules
{
	float QuotaLag(const FDCPressureInputs& In, const FDCPressureTuning& T)
	{
		const float Fraction = FMath::Clamp(In.SectorElapsed / FMath::Max(In.SectorDuration, 1.f), 0.f, 1.f);
		const float Expected = Fraction * In.QuotaPoints;
		const float Behind = Expected - static_cast<float>(In.PhotoPoints);
		return FMath::Clamp(Behind / FMath::Max(In.QuotaPoints * T.FullLagFraction, 1.f), 0.f, 1.f);
	}

	FDCPressureDecision Decide(const FDCPressureInputs& In, const FDCPressureTuning& T)
	{
		FDCPressureDecision Out;
		Out.Lag = QuotaLag(In, T);
		const int32 Crew = FMath::Max(In.PlayersStanding, 1);
		Out.TargetAlive = FMath::Min(T.HardCap, T.BaseAlive + T.PerPlayer * (Crew - 1) + FMath::RoundToInt(T.LagBonus * Out.Lag));
		const float Gap = T.PackGapSeconds * (1.f - 0.5f * Out.Lag);
		const bool bQuiet = In.SectorElapsed < T.StartSeconds || In.SinceCollapse < T.ReliefSeconds || In.SinceDown < T.ReliefSeconds
			|| In.SinceLastPack < Gap || In.PlayersStanding <= 0;
		const int32 Room = Out.TargetAlive - In.Alive;
		if (bQuiet || Room <= 0)
		{
			return Out;
		}
		const int32 Wanted = FMath::RoundToInt(FMath::Lerp(static_cast<float>(T.PackMin), static_cast<float>(T.PackMax), Out.Lag));
		Out.PackSize = FMath::Clamp(Wanted, 1, Room);
		return Out;
	}
}
