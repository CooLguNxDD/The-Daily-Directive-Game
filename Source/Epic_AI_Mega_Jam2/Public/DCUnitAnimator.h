// R20 unit animation applier (advisor a-28): one per animated unit, local on every machine. It builds the unit's
// segmented voxel mesh with the unit material, reads the unit's motion, health and attack counter each frame, keeps the
// clip state (Rules/DCUnitAnim), and at the LOD rate writes one quaternion per segment into custom primitive data
// (M_DCVoxelUnit turns each segment about its joint in the vertex shader) and hands the root move back to the unit.
// No ticking of its own, no extra components or draws; the unit calls Advance from its tick.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rules/DCUnitAnim.h"
#include "DCUnitAnimator.generated.h"

class UProceduralMeshComponent;
struct FDCEnemyDef;
struct FDCVoxelUnitMesh;

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCUnitAnimator : public UActorComponent
{
	GENERATED_BODY()

public:
	UDCUnitAnimator();

	/** A unit with a +UnitGaits row gets an animator; the mesh section goes on Mesh (segmented when the model has parts
	 *  and the unit material loads, else the plain voxel mesh that still bobs, leans, falls and spawns). */
	static UDCUnitAnimator* Attach(AActor& Unit, UProceduralMeshComponent& MeshComp, const FDCEnemyDef& Def);

	/** Every frame: motion from the unit's own location (host or replicated), flinch from a health drop, attack from the
	 *  replicated attack counter, death from !bAlive. */
	void Advance(float DeltaSeconds, const FVector& Location, float Yaw, float Health, uint8 AttackSeq, bool bAlive);
	/** The root's move for the model component (model space, cm and degrees, before the model scale). */
	FVector RootOffset() const { return Pose.RootOffset; }
	FRotator RootRot() const { return Pose.RootRot; }
	/** Puts the root's move on the model component (relative to the unit, at the row's offset and scale). */
	void ApplyRoot(const FDCEnemyDef& Def);
	/** Seconds the death clip takes (0 = none): the unit bursts into crumbs after it. */
	float DeathSeconds() const;
	/** Plays a clip now if the state table lets it start (evidence flows drive states directly). */
	void Play(EDCUnitClip Next);
	/** Evidence (-dcanimshot): hold this ground speed instead of the measured one (negative = measure). */
	float ForcedSpeedCm = -1.f;
	/** Evidence: pose every frame at full detail whatever the player's camera distance (the showcase films its own way). */
	bool bForceFullLod = false;
	bool IsSegmented() const { return bSegmented; }
	EDCUnitClip GetClip() const { return Clip; }

private:
	const FDCUnitGait* Gait() const;
	const FDCUnitClip* ClipRow(EDCUnitClip Which) const;
	void Apply();

	TWeakObjectPtr<UProceduralMeshComponent> Mesh;
	TArray<FName> Roles;
	FName GaitId;
	bool bSegmented = false;
	bool bStarted = false;
	FVector Last = FVector::ZeroVector;
	float LastYaw = 0.f;
	float LastHealth = -1.f;
	uint8 SeenAttack = 0;
	float Phase = 0.f;
	float Speed = 0.f;
	/** Advisor a-32: speed measured over the window between location changes (clients see 15-20 Hz steps). */
	float SpeedWindow = 0.f;
	float MeasuredSpeed = 0.f;
	float Turn = 0.f;
	float Time = 0.f;
	float Accum = 0.f;
	EDCUnitClip Clip = EDCUnitClip::None;
	float ClipTime = 0.f;
	FDCUnitPose Pose;
	TArray<float> Cpd;
};
