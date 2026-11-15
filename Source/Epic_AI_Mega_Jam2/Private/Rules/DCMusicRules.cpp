#include "Rules/DCMusicRules.h"

namespace DCMusicRules
{
	/** One blast's share of BlastWeight: TelegraphShare while it is coming, then a linear fade over BlastHoldSeconds. */
	static float BlastShare(const FDCMusicTuning& T, const FDCMusicBlast& B)
	{
		if (B.DistanceCm > T.BlastRadiusCm || B.SinceTelegraph < 0.f || B.Age > T.BlastHoldSeconds)
		{
			return 0.f;
		}
		if (B.Age < 0.f)
		{
			return T.TelegraphShare;
		}
		return 1.f - B.Age / FMath::Max(T.BlastHoldSeconds, 0.01f);
	}

	float Intensity(const FDCMusicTuning& T, int32 HostilesNear, TConstArrayView<FDCMusicBlast> Blasts)
	{
		const float Hostile = T.HostileWeight * FMath::Clamp(HostilesNear / FMath::Max(T.HostilesForFull, 1.f), 0.f, 1.f);
		float Blast = 0.f;
		for (const FDCMusicBlast& B : Blasts)
		{
			Blast += BlastShare(T, B);
		}
		return FMath::Clamp(Hostile + T.BlastWeight * Blast, 0.f, 1.f);
	}

	float Smooth(const FDCMusicTuning& T, float Current, float Raw, float Dt)
	{
		const float Tau = FMath::Max(Raw > Current ? T.RiseSeconds : T.FallSeconds, 0.01f);
		return Current + (Raw - Current) * (1.f - FMath::Exp(-FMath::Max(Dt, 0.f) / Tau));
	}

	int32 PickLayer(const TArray<FDCMusicLayer>& Layers, int32 Current, float Intensity, float HeldSeconds)
	{
		if (Layers.Num() == 0)
		{
			return INDEX_NONE;
		}
		int32 Up = 0;
		for (int32 i = 0; i < Layers.Num(); ++i)
		{
			Up = Layers[i].EnterAt <= Intensity ? i : Up;
		}
		if (!Layers.IsValidIndex(Current) || Up >= Current)
		{
			return Up; // fresh pick, escalation, or staying put
		}
		const FDCMusicLayer& Cur = Layers[Current];
		return HeldSeconds >= Cur.MinHoldSeconds && Intensity <= Cur.ExitAt ? Up : Current;
	}

	int32 NextSlot(int32 Slot, int32 Num)
	{
		return Num > 0 ? (Slot + 1) % Num : 0;
	}
}
