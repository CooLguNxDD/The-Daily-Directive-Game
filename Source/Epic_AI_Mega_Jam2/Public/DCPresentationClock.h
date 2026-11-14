// Per-machine presentation clock (R21, advisor CP6): slow motion for one player is presentation, never simulation.
// Cosmetic systems on this machine (voxel fx puffs, character animation) read Dilation(); gameplay, physics, the
// network and every other machine keep real time. Solo Last Frame still slows the shared clock (GameMode).
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DCPresentationClock.generated.h"

class UDCPresentationClock;

/** Start of every frame (pre-physics): puts the lagged visuals back so gameplay code always sees the real ones. */
struct FDCPresentationRestoreTick : public FTickFunction
{
	UDCPresentationClock* Owner = nullptr;
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("DCPresentationRestoreTick"); }
};

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
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	/** Puts every lagged visual component back to its real relative transform. */
	void RestoreVisuals();
	/** Presentation time this machine shows moving actors at (equals world time when nothing is slowed). */
	double GetPresentTime() const { return PresentTime; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDCPresentationClock, STATGROUP_Tickables); }

private:
	float Target = 1.f;
	float Current = 1.f;
	float Ramp = 0.3f;
	double HoldUntilReal = 0.0;
	double LastReal = -1.0;
	float AppliedAnim = 1.f;

	/** Presentation lag (CP8 major): while slowed, every other moving actor (enemies, teammates) is drawn at
	 *  PresentTime from its recorded root path: its visual children move, the actor (collision, gameplay, network)
	 *  stays live. PresentTime runs at the dilation, then catches up at CatchUpRate once the hold ends. */
	bool bLagging = false;
	double PresentTime = 0.0;
	TMap<TWeakObjectPtr<AActor>, TArray<TPair<double, FTransform>>> History;
	TMap<TWeakObjectPtr<USceneComponent>, FTransform> RealRelative;
	FDCPresentationRestoreTick RestoreTick;
	void TrackAndLag(UWorld* W, double Now);
};
