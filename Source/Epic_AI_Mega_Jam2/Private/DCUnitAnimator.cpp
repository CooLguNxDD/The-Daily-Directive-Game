#include "DCUnitAnimator.h"

#include "Camera/PlayerCameraManager.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	/** Speed and turn smoothing (per second): replicated units move in 15-20 Hz steps, the read must not stutter. */
	constexpr float UnitAnimMotionEase = 8.f;
}

UDCUnitAnimator::UDCUnitAnimator()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UDCUnitAnimator* UDCUnitAnimator::Attach(AActor& Unit, UProceduralMeshComponent& MeshComp, const FDCEnemyDef& Def, float SpawnAgeSeconds)
{
	const UDCTuning& T = UDCTuning::Get();
	if (!DCUnitAnim::FindGait(T.UnitGaits, Def.Gait))
	{
		return nullptr;
	}
	const FDCVoxelUnitMesh* UnitMesh = DCVoxelModels::UnitMesh(Def.Model);
	UMaterialInterface* UnitMat = UnitMesh && UnitMesh->Roles.Num() > 0 ? Cast<UMaterialInterface>(T.UnitMaterial.TryLoad()) : nullptr;
	if (!UnitMesh)
	{
		return nullptr;
	}
	UDCUnitAnimator* Anim = NewObject<UDCUnitAnimator>(&Unit, TEXT("UnitAnimator"));
	Anim->RegisterComponent();
	Anim->Mesh = &MeshComp;
	Anim->GaitId = Def.Gait;
	Anim->bSegmented = UnitMat != nullptr;
	if (Anim->bSegmented)
	{
		const FDCVoxelMeshData& M = UnitMesh->Mesh;
		MeshComp.CreateMeshSection(0, M.Positions, M.Indices, M.Normals, M.UVs, UnitMesh->UV1, UnitMesh->UV2, {}, M.Colors, {}, false);
		MeshComp.SetMaterial(0, UnitMat);
		// Limbs swing outside the rest-pose bounds (advisor a-28): keep them from culling at the screen edge.
		MeshComp.SetBoundsScale(1.5f);
		Anim->Roles = UnitMesh->Roles;
	}
	else
	{
		const FDCVoxelMeshData& M = UnitMesh->Mesh;
		MeshComp.CreateMeshSection(0, M.Positions, M.Indices, M.Normals, M.UVs, M.Colors, {}, false);
		MeshComp.SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
		UE_LOG(LogTemp, Display, TEXT("DCANIM %s: %s unsegmented (parts %d, unit material %s)"), *Unit.GetName(), *Def.Model.ToString(),
			UnitMesh->Roles.Num(), *T.UnitMaterial.ToString());
	}
	Anim->Last = Unit.GetActorLocation();
	Anim->LastYaw = Unit.GetActorRotation().Yaw;
	// Spread pose updates of a pack across frames.
	Anim->Accum = FMath::FRand();
	// Advisor a-32: a unit that becomes relevant on a client mid-walk must not replay its drop-in: the spawn clip plays
	// only while the unit is younger than the clip (server spawn time, replicated once).
	const FDCUnitClip* SpawnRow = Anim->ClipRow(EDCUnitClip::Spawn);
	const bool bFresh = !SpawnRow || SpawnAgeSeconds < SpawnRow->Seconds;
	if (bFresh)
	{
		Anim->Play(EDCUnitClip::Spawn);
	}
	// The spawn move's first frame now, so the unit never shows standing before it (a drop starts in the air).
	if (const FDCUnitGait* G = bFresh ? Anim->Gait() : nullptr)
	{
		FDCUnitAnimInput In;
		In.Clip = Anim->Clip;
		DCUnitAnim::Pose(*G, Anim->ClipRow(Anim->Clip), In, Anim->Roles, Anim->Pose);
		Anim->Apply();
		Anim->ApplyRoot(Def);
	}
	return Anim;
}

const FDCUnitGait* UDCUnitAnimator::Gait() const
{
	return DCUnitAnim::FindGait(UDCTuning::Get().UnitGaits, GaitId);
}

const FDCUnitClip* UDCUnitAnimator::ClipRow(EDCUnitClip Which) const
{
	const FDCUnitGait* G = Gait();
	return G ? DCUnitAnim::FindClip(UDCTuning::Get().UnitClips, DCUnitAnim::ClipId(*G, Which)) : nullptr;
}

float UDCUnitAnimator::DeathSeconds() const
{
	const FDCUnitClip* Row = ClipRow(EDCUnitClip::Death);
	return Row ? FMath::Clamp(Row->Seconds, 0.f, 3.f) : 0.f;
}

void UDCUnitAnimator::Play(EDCUnitClip Next)
{
	const FDCUnitClip* Current = ClipRow(Clip);
	if (!ClipRow(Next) || !DCUnitAnim::CanStart(Clip, ClipTime, Current ? Current->Seconds : 0.f, Next))
	{
		return;
	}
	Clip = Next;
	ClipTime = 0.f;
	// Co-op parity evidence (advisor a-32, -dcenemytrace): every clip start with the machine's role, so host and client
	// counts of Spawn/Attack/Flinch/Death can be compared per run.
	static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("dcenemytrace"));
	if (bTrace && GetOwner())
	{
		static const TCHAR* Names[] = {TEXT("None"), TEXT("Attack"), TEXT("Flinch"), TEXT("Spawn"), TEXT("Death")}; // EDCUnitClip order
		const int32 Index = static_cast<int32>(Next);
		UE_LOG(LogTemp, Display, TEXT("DCANIM clip=%s role=%s unit=%s"), Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?"),
			GetOwner()->HasAuthority() ? TEXT("host") : TEXT("client"), *GetOwner()->GetName());
	}
}

void UDCUnitAnimator::Advance(float DeltaSeconds, const FVector& Location, float Yaw, float Health, uint8 AttackSeq, bool bAlive)
{
	const FDCUnitGait* G = Gait();
	if (!G || DeltaSeconds <= 0.f)
	{
		return;
	}
	// States from replicated facts (every machine): death, a health drop, a new attack.
	if (!bAlive)
	{
		Play(EDCUnitClip::Death);
	}
	if (bStarted && bAlive && Health < LastHealth - 0.01f)
	{
		Play(EDCUnitClip::Flinch);
	}
	if (bStarted && AttackSeq != SeenAttack && bAlive)
	{
		Play(EDCUnitClip::Attack);
	}
	bStarted = true;
	LastHealth = Health;
	SeenAttack = AttackSeq;

	// Motion: smoothed ground speed and yaw rate; the stride phase follows the smoothed speed so 15 Hz updates on a
	// client do not jerk the legs.
	// Advisor a-32: on a client the location steps at the net rate, so speed is measured over the window since the last
	// change (per frame it read ~75% of the real speed and spiked on step frames: sliding legs). The host moves every
	// frame, so its window is one frame. No clamp: the window keeps the value real.
	SpeedWindow += DeltaSeconds;
	const float Step = FVector::Dist2D(Location, Last);
	if (Step > 0.5f)
	{
		MeasuredSpeed = Step / FMath::Max(SpeedWindow, 1e-3f);
		SpeedWindow = 0.f;
		Last = Location;
	}
	else if (SpeedWindow > 0.3f)
	{
		MeasuredSpeed = 0.f; // no step for longer than any net interval: standing
		SpeedWindow = 0.f;
	}
	const float Turned = FRotator::NormalizeAxis(Yaw - LastYaw) / DeltaSeconds;
	LastYaw = Yaw;
	const float Ease = FMath::Clamp(DeltaSeconds * UnitAnimMotionEase, 0.f, 1.f);
	Speed = ForcedSpeedCm >= 0.f ? ForcedSpeedCm : FMath::Lerp(Speed, MeasuredSpeed, Ease);
	Turn = FMath::Lerp(Turn, FMath::Clamp(Turned, -720.f, 720.f), Ease);
	Phase = DCUnitAnim::AdvancePhase(Phase, Speed * DeltaSeconds, *G);
	Time += DeltaSeconds;
	ClipTime += DeltaSeconds;
	const FDCUnitClip* Row = ClipRow(Clip);
	if (Clip != EDCUnitClip::None && (!Row || (ClipTime >= Row->Seconds && !DCUnitAnim::HoldsEnd(Clip))))
	{
		Clip = EDCUnitClip::None;
		Row = nullptr;
	}

	// LOD by distance from the local camera; a machine without one (dedicated server) never poses.
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Cam = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Cam && !bForceFullLod)
	{
		return;
	}
	const float CamDist = bForceFullLod || !Cam ? 0.f : FVector::Dist(Cam->GetCameraLocation(), Location);
	const FDCAnimLod& Lod = DCUnitAnim::Lod(UDCTuning::Get().AnimLods, CamDist);
	Accum += DeltaSeconds;
	if (Lod.Hz > 0.f && Accum < 1.f / Lod.Hz)
	{
		return;
	}
	Accum = Lod.Hz > 0.f ? FMath::Fmod(Accum, 1.f / Lod.Hz) : 0.f;
	FDCUnitAnimInput In;
	In.Phase = Phase;
	In.SpeedCm = Speed;
	In.TurnDegPerSec = Turn;
	In.Time = Time;
	In.Clip = Clip;
	In.ClipTime = ClipTime;
	In.bLimbs = Lod.bLimbs;
	DCUnitAnim::Pose(*G, Row, In, Roles, Pose);
	Apply();
}

void UDCUnitAnimator::ApplyRoot(const FDCEnemyDef& Def)
{
	if (UProceduralMeshComponent* M = Mesh.Get())
	{
		M->SetRelativeLocationAndRotation(Def.ModelOffset + Pose.RootOffset * Def.ModelScale, Pose.RootRot);
	}
}

void UDCUnitAnimator::Apply()
{
	UProceduralMeshComponent* M = Mesh.Get();
	if (!M || !bSegmented)
	{
		return;
	}
	// Custom primitive data: slot 0 is the dissolve (unused here), then 4 floats (a quaternion) per segment.
	Cpd.SetNumZeroed(FDCVoxelModel::MaxSegments * 4);
	for (int32 i = 0; i < Pose.Segments.Num() && i < FDCVoxelModel::MaxSegments; ++i)
	{
		const FQuat& Q = Pose.Segments[i];
		Cpd[i * 4 + 0] = static_cast<float>(Q.X);
		Cpd[i * 4 + 1] = static_cast<float>(Q.Y);
		Cpd[i * 4 + 2] = static_cast<float>(Q.Z);
		Cpd[i * 4 + 3] = static_cast<float>(Q.W);
	}
	M->SetCustomPrimitiveDataFloatArray(1, TConstArrayView<float>(Cpd));
}
