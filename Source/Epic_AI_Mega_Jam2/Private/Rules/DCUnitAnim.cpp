#include "Rules/DCUnitAnim.h"

namespace
{
	/** Clip state table (an entry per EDCUnitClip, in enum order): priority is the enum order, Death is final. */
	struct FDCUnitClipRule
	{
		bool bHoldsEnd;
		FName FDCUnitGait::*Row;
	};
	const FDCUnitClipRule& UnitClipRule(EDCUnitClip Clip)
	{
		static const FDCUnitClipRule ClipRules[] = {
			{false, nullptr},              // None
			{false, &FDCUnitGait::Attack}, // Attack
			{false, &FDCUnitGait::Flinch}, // Flinch
			{false, &FDCUnitGait::Spawn},  // Spawn
			{true, &FDCUnitGait::Death},   // Death
		};
		static_assert(static_cast<int32>(UE_ARRAY_COUNT(ClipRules)) == static_cast<int32>(EDCUnitClip::Death) + 1, "one rule per clip");
		return ClipRules[FMath::Clamp(static_cast<int32>(Clip), 0, static_cast<int32>(UE_ARRAY_COUNT(ClipRules)) - 1)];
	}

	float UnitAnimWave(float Cycles)
	{
		return FMath::Sin(Cycles * UE_TWO_PI);
	}
}

namespace DCUnitAnim
{
	bool HoldsEnd(EDCUnitClip Clip)
	{
		return UnitClipRule(Clip).bHoldsEnd;
	}

	bool CanStart(EDCUnitClip Current, float CurrentTime, float CurrentSeconds, EDCUnitClip Next)
	{
		if (Next == EDCUnitClip::None || HoldsEnd(Current))
		{
			return false;
		}
		const EDCUnitClip Playing = CurrentTime >= CurrentSeconds ? EDCUnitClip::None : Current;
		return static_cast<uint8>(Next) >= static_cast<uint8>(Playing);
	}

	FName ClipId(const FDCUnitGait& Gait, EDCUnitClip Clip)
	{
		const FDCUnitClipRule& R = UnitClipRule(Clip);
		return R.Row ? Gait.*R.Row : NAME_None;
	}

	float Envelope(const FDCUnitClip& Clip, float ClipTime, bool bHoldEnd)
	{
		const float Seconds = FMath::Max(Clip.Seconds, 0.01f);
		if (ClipTime >= Seconds && !bHoldEnd)
		{
			return 0.f;
		}
		const float T = FMath::Clamp(ClipTime / Seconds, 0.f, 1.f);
		const float Rise = FMath::Clamp(Clip.RiseFrac, 0.f, 1.f);
		const float Hold = FMath::Clamp(Clip.HoldFrac, 0.f, 1.f - Rise);
		const float Fall = 1.f - Rise - Hold;
		if (T < Rise)
		{
			return FMath::SmoothStep(0.f, 1.f, T / Rise);
		}
		if (T <= Rise + Hold || Fall <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return 1.f - FMath::SmoothStep(0.f, 1.f, (T - Rise - Hold) / Fall);
	}

	float AdvancePhase(float Phase, float DistCm, const FDCUnitGait& Gait)
	{
		return FMath::Frac(Phase + FMath::Max(DistCm, 0.f) / FMath::Max(Gait.StrideCm, 1.f));
	}

	const FDCAnimLod& Lod(const TArray<FDCAnimLod>& Rows, float DistCm)
	{
		for (const FDCAnimLod& Row : Rows)
		{
			if (DistCm <= Row.MaxDistCm)
			{
				return Row;
			}
		}
		static const FDCAnimLod Default;
		return Rows.Num() > 0 ? Rows.Last() : Default;
	}

	void Pose(const FDCUnitGait& Gait, const FDCUnitClip* Clip, const FDCUnitAnimInput& In, const TArray<FName>& Roles, FDCUnitPose& Out)
	{
		const float MoveW = FMath::Clamp(In.SpeedCm / FMath::Max(Gait.MoveFadeCm, 1.f), 0.f, 1.f);
		const float Run = FMath::Clamp(In.SpeedCm / FMath::Max(Gait.RunSpeedCm, 1.f), 0.f, 1.f);
		const float Amp = FMath::Lerp(FMath::Clamp(Gait.WalkScale, 0.f, 1.f), 1.f, Run);
		const float W = Clip ? Envelope(*Clip, In.ClipTime, HoldsEnd(In.Clip)) : 0.f;
		const float Keep = Clip ? FMath::Lerp(1.f, FMath::Clamp(Clip->LocoKeep, 0.f, 1.f), W) : 1.f;
		const float Loco = MoveW * Keep;
		const float Idle = (1.f - MoveW) * Keep;
		const float Stride = UnitAnimWave(In.Phase);

		// Root: bob twice a stride, sway once, lean into speed and roll into turns; the clip's move on top.
		Out.RootOffset = FVector(0.f, 0.f, Gait.BobCm * Amp * Loco * FMath::Abs(Stride));
		Out.RootRot = FRotator(-Gait.LeanDeg * Run * Loco, 0.f,
			Gait.SwayDeg * Amp * Loco * Stride + Gait.TurnRollDeg * FMath::Clamp(In.TurnDegPerSec / 180.f, -1.f, 1.f) * Loco);
		if (Clip)
		{
			Out.RootOffset += Clip->RootOffsetCm * W;
			Out.RootRot += Clip->RootRot * W;
			Out.RootRot.Roll += Clip->ShakeDeg * W * UnitAnimWave(In.ClipTime * Clip->ShakeHz);
		}

		Out.Segments.Reset();
		for (const FName& Role : Roles)
		{
			const FDCLimbSwing* Limb = Gait.Limbs.FindByPredicate([Role](const FDCLimbSwing& L) { return L.Role == Role; });
			FRotator R = Limb ? Limb->Rest : FRotator::ZeroRotator;
			if (Limb && In.bLimbs)
			{
				const float Swing = UnitAnimWave(In.Phase + Limb->Phase) * Limb->SwingDeg * Amp * Loco
					+ UnitAnimWave(In.Time * Gait.IdleHz + Limb->Phase) * Limb->IdleDeg * Idle;
				R += Limb->Axis * Swing;
			}
			const FDCClipLimb* Target = Clip && In.bLimbs ? Clip->Limbs.FindByPredicate([Role](const FDCClipLimb& L) { return L.Role == Role; }) : nullptr;
			if (Target)
			{
				R += Target->Rot * W;
			}
			Out.Segments.Add(R.Quaternion());
		}
	}

	const FDCUnitGait* FindGait(const TArray<FDCUnitGait>& Rows, FName Id)
	{
		return Id.IsNone() ? nullptr : Rows.FindByPredicate([Id](const FDCUnitGait& R) { return R.Id == Id; });
	}

	const FDCUnitClip* FindClip(const TArray<FDCUnitClip>& Rows, FName Id)
	{
		return Id.IsNone() ? nullptr : Rows.FindByPredicate([Id](const FDCUnitClip& R) { return R.Id == Id; });
	}
}
