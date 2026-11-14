// Per-machine presentation clock (R21, advisor CP6): slow motion for one player is presentation, never simulation.
// Cosmetic systems on this machine (voxel fx puffs, character animation) read Dilation(); gameplay, physics, the
// network and every other machine keep real time. Solo Last Frame still slows the shared clock (GameMode).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DCPresentationClock.generated.h"

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCPresentationClock : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** This machine's cosmetic time scale (1 when nothing is slowed or no world). */
	static float Dilation(const UObject* WorldContext);
	/** Hold Dilation for RealSeconds, then ramp back to 1 over RampSeconds of real time. */
	void Begin(float InDilation, float RealSeconds, float RampSeconds = 0.3f);
	float Get() const { return Current; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDCPresentationClock, STATGROUP_Tickables); }

private:
	float Target = 1.f;
	float Current = 1.f;
	float Ramp = 0.3f;
	double HoldUntilReal = 0.0;
	double LastReal = -1.0;
	float AppliedAnim = 1.f;
};
