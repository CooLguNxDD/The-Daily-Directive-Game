// R20 unit animation as data (advisor a-28): every faction unit walks, runs, idles, attacks, flinches, dies and plays
// its faction's spawn move instead of sliding. Voxel models are cut into segments at forge time (voxel-forge `part`
// lines: LegL, LegR, ArmL, ArmR, Head, ...); this pure rule turns a gait row, a clip row and the unit's motion into one
// rotation per segment plus a root offset. The unit material (M_DCVoxelUnit) turns each segment about its joint in the
// vertex shader from custom primitive data, so a swarm costs no extra components or draws.
// Locomotion is continuous (phase from distance travelled, amplitude from speed); clips are an enum state with a
// priority table (Spawn, Attack, Flinch, Death). Everything runs locally on each machine from replicated motion and
// health, so clients need no extra replication beyond the attack counter.
#pragma once

#include "CoreMinimal.h"
#include "DCUnitAnim.generated.h"

/** The clip a unit plays over its locomotion. Order is the priority (a clip starts when it outranks the one playing). */
UENUM(BlueprintType)
enum class EDCUnitClip : uint8
{
	None,
	Attack,
	Flinch,
	Spawn,
	Death,
};

/** One limb's walk cycle: the role's segment swings about Axis by SwingDeg at Phase (cycles) of the stride. */
USTRUCT(BlueprintType)
struct FDCLimbSwing
{
	GENERATED_BODY()
	/** Segment role from the model's `part` lines (LegL, LegR, ArmL, ArmR, Head, ...). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Role;
	/** Swing axis weights (Pitch swings fore and aft for upright limbs, Yaw for a crawler's sideways legs). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FRotator Axis = FRotator(1.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float SwingDeg = 20.f;
	/** Offset in stride cycles (0.5 = opposite the other leg). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float Phase = 0.f;
	/** Held pose under the swing (a shambler's arms reach forward). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FRotator Rest = FRotator::ZeroRotator;
	/** Idle breathing on this limb while the unit stands (deg about Axis at the gait's IdleHz; 0 = still). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float IdleDeg = 0.f;
};

/** A faction's gait (one row per look, units name it with Gait= on their +Enemies row). */
USTRUCT(BlueprintType)
struct FDCUnitGait
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Id;
	/** Ground covered per full stride cycle (two steps); the phase advances with distance, so feet do not skate. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float StrideCm = 140.f;
	/** Speed (cm/s) of the full run read; walk amplitude below it. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float RunSpeedCm = 450.f;
	/** Swing amplitude at a walk, as a share of the run's (1 at RunSpeedCm). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float WalkScale = 0.6f;
	/** Speed (cm/s) below which the unit blends to idle. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float MoveFadeCm = 60.f;
	/** Body bob per step (cm, at run) and roll sway per stride (deg). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float BobCm = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float SwayDeg = 2.f;
	/** Forward lean at a run (deg) and roll into a turn per 180 deg/s of yaw (deg). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float LeanDeg = 6.f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float TurnRollDeg = 8.f;
	/** Idle breathing rate: limbs with an IdleDeg sway at IdleHz (with their own phase) while the unit stands. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float IdleHz = 0.5f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") TArray<FDCLimbSwing> Limbs;
	/** Clip rows (+UnitClips ids); none = that state plays nothing. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Spawn;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Attack;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Flinch;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Death;
};

/** A clip's target for one role (added on top of the locomotion pose, times the clip's envelope). */
USTRUCT(BlueprintType)
struct FDCClipLimb
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Role;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FRotator Rot = FRotator::ZeroRotator;
};

/** A one-shot move: an envelope (ease in to Rise, hold, ease out) scales the root and limb targets. */
USTRUCT(BlueprintType)
struct FDCUnitClip
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FName Id;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float Seconds = 0.5f;
	/** Envelope shares of the clip: ease in over RiseFrac, hold HoldFrac, ease out over the rest. RiseFrac 0 starts at
	 *  full weight (a spawn that settles); RiseFrac + HoldFrac = 1 ends at full weight (a death that stays down). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float RiseFrac = 0.3f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float HoldFrac = 0.1f;
	/** Share of the walk cycle that keeps playing under the clip (0 for spawn and death). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float LocoKeep = 1.f;
	/** Root move about the model pivot (the feet) and root offset (cm, model space: X forward, Z up). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FRotator RootRot = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") FVector RootOffsetCm = FVector::ZeroVector;
	/** Shake while the clip is on (deg of roll at ShakeHz): a twitch, a struggle out of the ground. */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float ShakeDeg = 0.f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float ShakeHz = 8.f;
	UPROPERTY(EditAnywhere, Config, Category = "Anim") TArray<FDCClipLimb> Limbs;
};

/** Animation level of detail by distance from the local camera (rows nearest first; the last row covers the rest). */
USTRUCT(BlueprintType)
struct FDCAnimLod
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float MaxDistCm = 3000.f;
	/** Pose updates per second (0 = every frame). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") float Hz = 0.f;
	/** Limbs animate (false = the root still bobs, the limbs hold their rest pose). */
	UPROPERTY(EditAnywhere, Config, Category = "Anim") bool bLimbs = true;
};

/** The unit's motion this update (local on every machine). */
struct FDCUnitAnimInput
{
	/** Stride phase in cycles (from distance travelled). */
	float Phase = 0.f;
	/** Smoothed ground speed (cm/s) and yaw rate (deg/s). */
	float SpeedCm = 0.f;
	float TurnDegPerSec = 0.f;
	/** Seconds since the unit appeared (idle breathing, shakes). */
	float Time = 0.f;
	EDCUnitClip Clip = EDCUnitClip::None;
	float ClipTime = 0.f;
	bool bLimbs = true;
};

/** A pose: the root's move about the feet, and one rotation per animated segment (index k - 1 for segment k). */
struct FDCUnitPose
{
	FVector RootOffset = FVector::ZeroVector;
	FRotator RootRot = FRotator::ZeroRotator;
	TArray<FQuat, TInlineAllocator<8>> Segments;
};

namespace DCUnitAnim
{
	/** Clip state table: may Next start while Current has played CurrentTime of CurrentSeconds? (Death is final; a clip
	 *  that ended counts as None; an equal clip restarts.) */
	EPIC_AI_MEGA_JAM2_API bool CanStart(EDCUnitClip Current, float CurrentTime, float CurrentSeconds, EDCUnitClip Next);
	/** Does this clip hold its last frame after it ends (death stays down)? */
	EPIC_AI_MEGA_JAM2_API bool HoldsEnd(EDCUnitClip Clip);
	/** The clip row id a gait names for a state (None when the gait plays nothing there). */
	EPIC_AI_MEGA_JAM2_API FName ClipId(const FDCUnitGait& Gait, EDCUnitClip Clip);
	/** Envelope weight 0..1 at ClipTime (clamped data: a bad row never produces weights outside 0..1). */
	EPIC_AI_MEGA_JAM2_API float Envelope(const FDCUnitClip& Clip, float ClipTime, bool bHoldEnd);
	/** Stride phase after moving DistCm (cycles, wrapped to 0..1). */
	EPIC_AI_MEGA_JAM2_API float AdvancePhase(float Phase, float DistCm, const FDCUnitGait& Gait);
	/** The LOD row for a camera distance (rows nearest first); a default row when there are none. */
	EPIC_AI_MEGA_JAM2_API const FDCAnimLod& Lod(const TArray<FDCAnimLod>& Rows, float DistCm);
	/** The pose for Roles (the model's segment roles in order) from a gait, the playing clip row (or null) and motion. */
	EPIC_AI_MEGA_JAM2_API void Pose(const FDCUnitGait& Gait, const FDCUnitClip* Clip, const FDCUnitAnimInput& In, const TArray<FName>& Roles, FDCUnitPose& Out);
	/** Row lookups by id. */
	EPIC_AI_MEGA_JAM2_API const FDCUnitGait* FindGait(const TArray<FDCUnitGait>& Rows, FName Id);
	EPIC_AI_MEGA_JAM2_API const FDCUnitClip* FindClip(const TArray<FDCUnitClip>& Rows, FName Id);
}
