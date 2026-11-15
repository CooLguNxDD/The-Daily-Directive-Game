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
	/** R8 evidence: switch through every weapon with the real Cycle input, fire and frame each. */
	void AddWeaponTour();
	/** R12 evidence: the host calls each listed strike on the nearest tower; telegraph, peak and aftermath frames. */
	void AddStrikeReel(const FString& Ids);
	void BuildScript();
	void AddCycle(const FString& Name);
	void AddObjectiveRun(const FString& S);
	void AddClashShot(const FString& S);
	void AddLineupShot();
	void AddSupplyCall();
	void AddRunFire();
	/** R24 evidence (-dcchipfire): sustained fire at one tower wall; fixed-pose frames before, during and after. */
	void AddChipFire();
	FVector ChipAim = FVector::ZeroVector;
	FVector ChipEye = FVector::ZeroVector;
	int32 ChipLot = INDEX_NONE;
	void AddBounds();
	void AddAim();
	void LogAim(const TCHAR* Tag);
	void AddSwarmed();
	void AddIndoors();
	void SideCam(bool bOn);
	UPROPERTY() TObjectPtr<class ACameraActor> EvidenceCam;
	int32 IndoorLot = INDEX_NONE;
	FVector IndoorDoor = FVector::ZeroVector;
	FVector SupplyAt = FVector::ZeroVector;
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
	float ObjStrikeRangeCm = 2500.f; // pilot only: it holds this far from a marked tower and calls the laser on its base
	float ObjStrikeFarCm = 6000.f; // pilot only: farther than that too, when its view of the base is clear
	float ObjStrikeSightStopCm = 800.f; // the sight check ends this short of the base (the target tower itself)
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
