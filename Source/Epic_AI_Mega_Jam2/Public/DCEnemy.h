// Simple-shape enemies. Kind data lives in UDCTuning::Enemies; the host runs one behaviour per kind, looked up by
// id in a registry (Swarmer, Spotlight, Walker). Clients only see replicated movement and state.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DCEnemy.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USpotLightComponent;
class ADCCharacter;
struct FDCEnemyDef;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCEnemy : public APawn
{
	GENERATED_BODY()

public:
	/** Server time this unit spawned (replicated once; -1 = unknown). */
	float GetSpawnServerTime() const { return SpawnServerTime; }
	ADCEnemy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Host: set before FinishSpawning. */
	void InitKind(FName InKind) { Kind = InKind; }

	bool IsAlive() const { return Health > 0.f; }
	/** Strobe Flash stun (replicated so every machine tags it STUNNED). */
	bool IsStunned(float Now) const { return Now < StunnedUntil; }
	bool IsWalker() const;
	FName GetKind() const { return Kind; }
	/** R20: the unit's animator (null for units without a gait row). */
	class UDCUnitAnimator* GetAnimator() const { return Anim; }
	const FDCEnemyDef* GetDef() const;
	/** R3: the unit's faction (its +Enemies row); None = the regime's machines of old (they fight the crew only). */
	FName GetFaction() const;
	/** Host: squad membership for formations (slot offsets from the squad's goal). */
	void HostSetSquad(int32 InSquad, int32 InSlot, int32 InSize) { Squad = InSquad; Slot = InSlot; SquadSize = InSize; }
	/** Host: the nearest live foe (a player this faction fights, or a hostile faction's unit) within Range. */
	AActor* FindFoe(float Range) const;
	/** Host: walk the street toward Goal (cheap: voxel column tops and solid tests, no sweeps; advisor a-2). */
	/** Closest a ground unit steps to a standing player (body + capsule + FactionMix.CrewContactMarginCm). */
	float CrewContactCm() const;
	void GroundMove(const FVector& Goal, float Speed, float DeltaSeconds, float Hover);
	/** Host: hit a foe (player or unit) in reach: melee at AttackRange, or a tracer shot when ShotRange > 0. */
	void HostAttack(AActor* Victim, float Now);
	/** Formation slot offset (world, XY) from the squad goal along a heading. */
	FVector SlotOffset(const FDCEnemyDef& Def, const FVector& Facing) const;
	TWeakObjectPtr<AActor> Foe;
	float NextRetarget = 0.f;
	int32 Squad = 0;
	int32 Slot = 0;
	int32 SquadSize = 1;

	// Behaviour helpers (host).
	ADCCharacter* FindTarget(float Range, bool bPreferPhotographers) const;
	void SteerTowards(const FVector& Goal, float Speed, float DeltaSeconds, float Hover);
	void TryAttack(ADCCharacter* Target, float Now);
	/** Host, big-machine ranged shot (RangedRangeCm rows): starts a charge on a crew member in sight, fires when it ends.
	 *  True while charging (the unit holds still). */
	bool HostTickRanged(const FDCEnemyDef& Def, ADCCharacter* Target, float Now);
	/** Any machine: the charge sight line is up (evidence frames). */
	bool IsRangedCharging() const;
	/** Host: a sound every machine hears at this enemy. */
	void Cue(FName CueName) { MulticastCue(CueName); }
	/** Host: Strobe Flash gear stuns the enemy until this server time. */
	void HostStun(float Until) { StunnedUntil = FMath::Max(StunnedUntil, Until); Cue(TEXT("HitMarker")); }
	/** Host: an active decoy beacon (Decoy Paparazzi) in range, if any. */
	bool FindLure(float Range, FVector& Out) const;

	FVector Heading = FVector::ForwardVector;
	float NextAttackTime = 0.f;
	float NextCueTime = 0.f;
	bool bRushing = false;
	/** Local clock until which steering keeps climbing after a wall hit (host). */
	float ClimbUntil = 0.f;
	float NextTraceTime = 0.f;
	UPROPERTY(Replicated) float StunnedUntil = 0.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Eye;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LegA;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LegB;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LegC;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Spot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> GlowLight;

	/** Spotlight: where the beam points (replicated so every machine draws the same cone). */
	UPROPERTY(Replicated) FVector_NetQuantize LightTarget;

private:
	UFUNCTION() void OnRep_Health();
	UFUNCTION(NetMulticast, Unreliable) void MulticastCue(FName Cue);
	/** Every machine draws the shot's tracer in the faction's cue colour. */
	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize From, FVector_NetQuantize To);
	/** Ranged charge and shot (every machine): the sight line and cue, then the beam and the blast. */
	UFUNCTION(NetMulticast, Reliable) void MulticastRangedCharge(FVector_NetQuantize From, FVector_NetQuantize To, float Seconds);
	UFUNCTION(NetMulticast, Reliable) void MulticastRangedFire(FVector_NetQuantize From, FVector_NetQuantize To, float BlastCm);
	void HostRangedFire(const FDCEnemyDef& Def, float Now);
	/** Local: redraw the charge sight line while it lasts. */
	void TickRangedLine(const FDCEnemyDef& Def);
	float RangedChargeEnd = -1.f; // host server time
	FVector RangedAt = FVector::ZeroVector;
	FVector ChargeLineFrom = FVector::ZeroVector;
	FVector ChargeLineTo = FVector::ZeroVector;
	double ChargeLineEndReal = -1.0;
	double NextChargeLineReal = 0.0;
	/** Gait read for rigid voxel models (local, every machine): bob and sway while the actor moves. */
	void TickGait(float DeltaSeconds);
	/** The rigid bob and sway for units without a gait row (before R20). */
	void TickBob(const FDCEnemyDef& Def, float DeltaSeconds);
	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> ModelComp;
	/** R20 (h-anim): poses the model's segments from motion, health and AttackSeq on every machine; null without a gait. */
	UPROPERTY() TObjectPtr<class UDCUnitAnimator> Anim;
	/** R20: bumped by the host per attack so every machine plays the attack clip (melee has no multicast). */
	UPROPERTY(Replicated) uint8 AttackSeq = 0;
	/** Server time the unit spawned (initial-only replication): late-relevant clients skip the spawn clip (a-32). */
	UPROPERTY(Replicated) float SpawnServerTime = -1.f;
	/** R20: the crumb burst waits for the death clip; EndPlay bursts if the actor goes first. */
	void BurstBody();
	bool bBodyBurst = false;
	FTimerHandle BurstTimer;
	FVector GaitLast = FVector::ZeroVector;
	float GaitPhase = 0.f;
	FVector SmoothModel = FVector::ZeroVector;
	void Die(AController* Killer);
	void BuildVisual();
	bool BuildModel(const struct FDCEnemyDef& Def);
	void PlayDeathFx();

	UPROPERTY(Replicated) FName Kind;
	UPROPERTY(ReplicatedUsing = OnRep_Health) float Health = 1.f;
	bool bDeathFxPlayed = false;
};
