#include "DCAnimInstance.h"
#include "DCCharacter.h"
#include "DCTuning.h"

#include "Animation/AnimNodeBase.h"
#include "BonePose.h"

void FDCAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	// Game thread: copy what the worker needs. The aim pose holds while the rifle is out and the draw is over.
	const ADCCharacter* Me = Cast<ADCCharacter>(InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr);
	const FDCAimTuning& A = UDCTuning::Get().Aim;
	const bool bOn = Me && Me->GetSlot() == EDCSlot::Rifle && !Me->IsDowned() && Me->IsRifleLocoActive();
	Weight = FMath::FInterpTo(Weight, bOn ? 1.f : 0.f, DeltaSeconds, A.PoseBlendSpeed);
	const float Target = Me ? FMath::Clamp(Me->GetAimPitch(), -A.MaxPitchDegrees, A.MaxPitchDegrees) : 0.f;
	PitchDegrees = FMath::FInterpTo(PitchDegrees, Target, DeltaSeconds, A.PitchFollowSpeed);
	Axis = A.PitchAxis.GetSafeNormal();
	Bones = A.PitchBones;
	Shares = A.PitchShares;
}

bool FDCAnimProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	EvaluateAnimationNode_WithRoot(Output, InRootNode);
	if (InRootNode != GetRootNode() || Weight <= KINDA_SMALL_NUMBER || Bones.Num() == 0 || Axis.IsNearlyZero())
	{
		return true;
	}
	// Component space, strictly parent first: each spine bone turns about the component axis by its share of the pitch;
	// the bones above inherit the turn (FCSPose computes a child lazily from its already-set parent).
	const FBoneContainer& Required = Output.Pose.GetBoneContainer();
	FCSPose<FCompactPose> CS;
	CS.InitPose(Output.Pose);
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const int32 MeshIndex = Required.GetPoseBoneIndexForBoneName(Bones[i]);
		if (MeshIndex == INDEX_NONE)
		{
			continue; // stripped at this LOD
		}
		const FCompactPoseBoneIndex Bone = Required.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Bone == INDEX_NONE)
		{
			continue;
		}
		const float Share = Shares.IsValidIndex(i) ? Shares[i] : 1.f / Bones.Num();
		FTransform T = CS.GetComponentSpaceTransform(Bone);
		T.SetRotation(FQuat(Axis, FMath::DegreesToRadians(PitchDegrees * Share * Weight)) * T.GetRotation());
		CS.SetComponentSpaceTransform(Bone, T);
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CS, Output.Pose);
	return true;
}
