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
	ADCEnemy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Host: set before FinishSpawning. */
	void InitKind(FName InKind) { Kind = InKind; }

	bool IsAlive() const { return Health > 0.f; }
	/** Strobe Flash stun (replicated so every machine tags it STUNNED). */
	bool IsStunned(float Now) const { return Now < StunnedUntil; }
	bool IsWalker() const;
	FName GetKind() const { return Kind; }
	const FDCEnemyDef* GetDef() const;

	// Behaviour helpers (host).
	ADCCharacter* FindTarget(float Range, bool bPreferPhotographers) const;
	void SteerTowards(const FVector& Goal, float Speed, float DeltaSeconds, float Hover);
	void TryAttack(ADCCharacter* Target, float Now);
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

	/** Spotlight: where the beam points (replicated so every machine draws the same cone). */
	UPROPERTY(Replicated) FVector_NetQuantize LightTarget;

private:
	UFUNCTION() void OnRep_Health();
	UFUNCTION(NetMulticast, Unreliable) void MulticastCue(FName Cue);
	void Die(AController* Killer);
	void BuildVisual();
	void PlayDeathFx();

	UPROPERTY(Replicated) FName Kind;
	UPROPERTY(ReplicatedUsing = OnRep_Health) float Health = 1.f;
	bool bDeathFxPlayed = false;
};
