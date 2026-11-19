#pragma once

#include "CoreMinimal.h"

/** Building HP (human 16:32): pure rules, no engine state. A tower takes structural damage from every strike that
 *  reaches its footprint; the strike may bite its base (and so topple it) only once the damage reaches its health.
 *  Numbers come from FDCVoxelTuning (Tuning=). */
namespace DCTowerHealth
{
	/** Health of a tower: PerFloor x floors x footprint scale (footprint cells / 9), clamped to [Min, Max]. */
	inline float Health(int32 Floors, int32 SizeX, int32 SizeY, float PerFloor, float Min, float Max)
	{
		const float Footprint = FMath::Sqrt(FMath::Max(SizeX * SizeY, 1) / 9.f);
		return FMath::Clamp(PerFloor * FMath::Max(Floors, 1) * Footprint, Min, FMath::Max(Min, Max));
	}

	/** Structural damage of one blast on a tower DistCm from its footprint (0 = inside): the strike's Damage at the
	 *  footprint, falling linearly to EdgeScale x Damage at the blast radius, 0 beyond it. */
	inline float Damage(float StrikeDamage, float DistCm, float BlastRadiusCm, float EdgeScale)
	{
		if (BlastRadiusCm <= 0.f || DistCm > BlastRadiusCm)
		{
			return 0.f;
		}
		const float T = FMath::Clamp(DistCm / BlastRadiusCm, 0.f, 1.f);
		return FMath::Max(StrikeDamage, 0.f) * FMath::Lerp(1.f, EdgeScale, T);
	}
}
