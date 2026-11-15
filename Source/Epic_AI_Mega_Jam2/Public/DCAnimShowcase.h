// R20 evidence only (-dcanimshot=<dir>): a side-view lineup of one unit per animated kind (-dcanimkinds=A,B,..), held
// in place (no AI, no damage) and stepped through spawn, idle, walk, run, attack, flinch and death on a fixed camera,
// with frames at fixed times (a still never proves motion: frames across time per state).
// -dcanimswarm=N (perf): N units of those kinds in rings round the player, held in place and animated at real time
// (walk, run, idle, attacks; each at its distance LOD) under a fixed overview camera, one DCANIMPERF line per second;
// -dcanimoff spawns the same swarm without animating it (the before half of the pair). Off without the flags.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DCAnimShowcase.generated.h"

class ADCEnemy;
class ACameraActor;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCAnimShowcase : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDCAnimShowcase, STATGROUP_Tickables); }

private:
	void Start();
	void StartSwarm(int32 Count);
	void TickSwarm(float DeltaTime);
	int32 SwarmCount = 0;
	bool bSwarm = false;
	float PerfClock = 0.f;
	double PerfAnimSeconds = 0.0;
	int32 PerfFrames = 0;
	FString Dir;
	float Clock = -1.f;
	float StartAt = 10.f;
	int32 NextShot = 0;
	int32 NextCue = 0;
	uint8 AttackSeq = 0;
	FVector Center = FVector::ZeroVector;
	UPROPERTY() TArray<TObjectPtr<ADCEnemy>> Units;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
};
