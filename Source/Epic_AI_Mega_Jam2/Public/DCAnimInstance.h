// R16 aim pose (advisor a-23): the player's AnimBP (ABP_DCPlayer, a duplicate of the template ABP_Unarmed) is
// reparented to this class. Its graph is untouched; after the graph evaluates, the native proxy bends the spine chain
// by the pawn's aim pitch (replicated, so every machine shows it) while the rifle is out. Bones, shares, axis, limits
// and blend speed are data (Aim= in Config/DefaultGame.ini).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "DCAnimInstance.generated.h"

struct FDCAnimProxy : public FAnimInstanceProxy
{
	FDCAnimProxy() = default;
	explicit FDCAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

	/** Copied on the game thread in PreUpdate; evaluation (a worker) reads only these. */
	float PitchDegrees = 0.f;
	float Weight = 0.f;
	FVector Axis = FVector(1.f, 0.f, 0.f);
	TArray<FName> Bones;
	TArray<float> Shares;
};

UCLASS(Transient, NotBlueprintable)
class EPIC_AI_MEGA_JAM2_API UDCAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FDCAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FDCAnimProxy*>(InProxy); }
};
