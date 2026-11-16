// R22 card companions: a gun drone that orbits its photographer and a turret the player sets down (Interact). One
// pooled actor class for both: the row's Mover (Rules/DCCompanionRules) says where it stands. The host owns them
// (activation, targets, damage); every machine places its copy locally from the replicated owner and anchor, and the
// shots arrive as a cosmetic multicast (tracer and cue). The pool (UDCCompanions) reuses actors between sectors.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "DCCompanion.generated.h"

class ADCCharacter;
class ADCPlayerState;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
struct FDCCompanionDef;

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCCompanion : public AActor
{
	GENERATED_BODY()

public:
	ADCCompanion();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Host: put this pooled actor to work as Kind for Owner. Home anchors a Fixed mover; Slot of Slots spreads
	 *  orbiters of one owner round the lap. */
	void HostActivate(FName InKind, ADCPlayerState* InOwner, const FVector& InHome, int32 InSlot, int32 InSlots, float Now);
	/** Host: back to the pool (hidden, no tick). */
	void HostRelease();
	bool IsActive() const { return bActive; }
	FName GetKind() const { return Kind; }
	const ADCPlayerState* GetOwnerState() const { return OwnerState; }
	float GetActivatedAt() const { return ActivatedAt; }
	const FDCCompanionDef* GetDef() const;

private:
	UFUNCTION() void OnRep_State();
	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize From, FVector_NetQuantize To);
	/** Builds the look for Kind (model or primitive) once per kind; shows or hides by bActive. */
	void ApplyState();
	/** Where the anchor is now (owner's pawn or home); false when there is none (owner without a pawn). */
	bool Anchor(const FDCCompanionDef& Def, FVector& Out) const;
	void HostShoot(const FDCCompanionDef& Def, const FVector& From, float Now);

	UPROPERTY(ReplicatedUsing = OnRep_State) FName Kind;
	UPROPERTY(ReplicatedUsing = OnRep_State) bool bActive = false;
	UPROPERTY(Replicated) TObjectPtr<ADCPlayerState> OwnerState;
	UPROPERTY(Replicated) FVector_NetQuantize Home;
	UPROPERTY(Replicated) uint8 Slot = 0;
	UPROPERTY(Replicated) uint8 Slots = 1;
	UPROPERTY(Replicated) float ActivatedAt = 0.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Primitive;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Model;
	FName BuiltKind;
	float NextShotTime = 0.f;
	/** Facing: toward the last shot for a moment, else along the orbit. */
	FVector LastAim = FVector::ForwardVector;
	float LastShotAt = -100.f;
};

/** Host pool of companion actors (rule 8: reused between sectors, prewarmed, bounded by CompanionPoolMax). */
UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCCompanions : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDCCompanions* Get(const UObject* WorldContext);
	/** Host: spawn idle actors up to N (sector start, before the first hot moment). */
	void Prewarm(int32 N);
	/** Host: an idle pooled actor (a new one while under the cap), or null when the pool is full. */
	ADCCompanion* Acquire();
	/** Host: every companion back to the pool (sector end, draft, reel). */
	void ReleaseAll();
	/** Active companions of one owner and kind, oldest first. */
	TArray<ADCCompanion*> ActiveOf(const ADCPlayerState* Owner, FName Kind) const;

	/** Host: Count orbiting companions of Def for the owner (sector start). Returns how many went up. */
	int32 HostLaunchOrbiters(ADCPlayerState& Owner, const FDCCompanionDef& Def, int32 Count);
	/** Host: set a Fixed companion down ahead of the player when the ground there is valid (the oldest of the owner
	 *  is recalled past MaxAlive). False (nothing placed) on bad ground, near a downed teammate, or a full pool. */
	/** Called = the call-in list's aim point (already clamped to the entry's range); null = ahead of the player (Interact).
	 *  Same placement rules either way: ground under the spot within MaxStepCm, clear line from the player. */
	bool HostDeploy(ADCCharacter& Player, ADCPlayerState& Owner, const FDCCompanionDef& Def, const FVector* Called = nullptr);

private:
	UPROPERTY() TArray<TObjectPtr<ADCCompanion>> Pool;
};
