// Scripted play session for evidence and smoke runs: `-dcdemo` on the command line. It plays the core loop through
// the real Enhanced Input pipeline (continuous input injection on the local player subsystem): fire the rifle, call
// a strike on a tower, run toward it, raise the camera and snap the blast in the Peak Cinema window. Screenshots go
// to `-dcshots=<dir>` (default Saved/DemoShots); `-dcdemoquit` exits when the script ends. Never active in normal play.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCTypes.h"
#include "DCDemoPilot.generated.h"

class ADCBuilding;
class ADCOrbitalStrike;
class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCDemoPilot : public AActor
{
	GENERATED_BODY()

public:
	ADCDemoPilot();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static bool IsRequested();
	/** -dcmedic on a joined client: this machine runs a pilot that only revives downed teammates (E, real input). */
	static bool IsMedicRequested();

private:
	/** One script step: wait Delay real seconds after the previous step, then run Do. A step whose Do returns
	 *  false is retried every tick (a wait-until condition) until it returns true or times out. */
	struct FStep
	{
		float Delay = 0.f;
		TFunction<bool()> Do;
		float Timeout = 12.f;
	};

	void AddCueRoll();
	void AddRifleOnly();
	void BuildScript();
	void AddCycle(const FString& Name);
	void AddObjectiveRun(const FString& S);
	void AddClashShot(const FString& S);
	void AddLineupShot();
	TArray<TWeakObjectPtr<class ADCEnemy>> Lineup;
	FVector LineupMid = FVector::ZeroVector;
	FVector ClashMid = FVector::ZeroVector;
	uint8 ObjStage = 255;
	double ObjNextShotReal = 0.0;
	double ObjCheckReal = 0.0;
	FVector ObjLastPos = FVector::ZeroVector;
	int32 ObjStrikePhase = 0;
	bool bObjDefending = false;
	float ObjDefendCm = 700.f; // pilot only: self-defence radius in -dcobjrun
	double ObjStrikeReal = 0.0;
	float ObjStrikeRangeCm = 3500.f; // pilot only: how close it walks before calling the laser on a marked tower
	void AddEnemyFight(const FString& Sector);
	void AddStratagemOnSwarm(const FString& Sector);
	void AddWidestFrame(const FString& Sector);
	/** -dcwatchwalker: frame the walker through the viewfinder, snap it, then frame the tower it condemns. */
	void AddWalkerWatch();
	void AddWaitPhase(EDCRunPhase Phase, float Timeout);
	void Add(float Delay, TFunction<bool()> Do, float Timeout = 12.f);
	void Press(FName Action, const FVector& Value = FVector(1, 0, 0));
	void Release(FName Action);
	void Tap(FName Action);
	void AimAt(const FVector& LookTarget);
	void Shot(const FString& Name);
	void LogPhoto();
	ADCBuilding* PickTower();
	ADCOrbitalStrike* LatestStrike() const;
	UEnhancedInputLocalPlayerSubsystem* Input() const;
	const UInputAction* Action(FName Name) const;

	TArray<FStep> Script;
	int32 Next = 0;
	/** First step of the reel section: a wiped run jumps there (the sector script would wait on phases that never come). */
	int32 ReelStep = INDEX_NONE;
	double StepReadyReal = -1.0;
	double StepStartReal = -1.0;
	FString ShotDir;
	bool bQuit = false;
	TWeakObjectPtr<ADCBuilding> Tower;
	double LastFrameAtReal = -1.0;
	TWeakObjectPtr<class ADCEnemy> Target;
	TSet<TObjectKey<ADCBuilding>> UsedTowers;
	int32 ShotIndex = 0;
	bool bAutoReviving = false;
	/** While waiting for the sector to end, the pilot shoots the nearest enemy (rifle, real input). */
	bool bDefend = false;
	bool bSpotlitFramed = false;
	TSet<TObjectKey<class ADCSupplyPod>> FramedPods;
	bool bDefendFiring = false;
	/** Backpedalling away from a swarmer at arm's length (only this pilot's own Move press is released). */
	bool bRetreating = false;
	void TickDefend();
	void TickMedic();
	bool bMedic = false;
	bool bMedicHolding = false;
	TWeakObjectPtr<class ADCCharacter> Patient;
};
