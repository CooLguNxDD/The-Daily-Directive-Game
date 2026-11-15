// R20 unit animation (h-anim): the pure gait rule (clip state table, envelope, stride, LOD), the shipped +UnitGaits /
// +UnitClips / +AnimLods data against the unit models' voxel-forge parts, and the segmented unit mesh. Pure: runs under
// -nullrhi with the real Config/DefaultGame.ini data. Looks are proven by frames (-dcanimshot), not here.
#include "DCTuning.h"
#include "Misc/AutomationTest.h"
#include "Rules/DCUnitAnim.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AnimFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCUnitAnimRulesTest, "Epic_AI_Mega_Jam2.UnitAnim.Rules", AnimFlags)
bool FDCUnitAnimRulesTest::RunTest(const FString&)
{
	using E = EDCUnitClip;
	// State table: death is final, an ended clip yields, higher priority interrupts, lower waits, equal restarts.
	TestFalse(TEXT("nothing replaces death"), DCUnitAnim::CanStart(E::Death, 0.1f, 1.f, E::Spawn));
	TestFalse(TEXT("death holds after it ends"), DCUnitAnim::CanStart(E::Death, 5.f, 1.f, E::Attack));
	TestTrue(TEXT("death interrupts a spawn"), DCUnitAnim::CanStart(E::Spawn, 0.1f, 1.f, E::Death));
	TestTrue(TEXT("a flinch interrupts an attack"), DCUnitAnim::CanStart(E::Attack, 0.1f, 1.f, E::Flinch));
	TestFalse(TEXT("an attack waits for a flinch"), DCUnitAnim::CanStart(E::Flinch, 0.1f, 1.f, E::Attack));
	TestTrue(TEXT("an attack starts after the flinch ended"), DCUnitAnim::CanStart(E::Flinch, 1.f, 1.f, E::Attack));
	TestFalse(TEXT("an attack never interrupts the spawn move"), DCUnitAnim::CanStart(E::Spawn, 0.2f, 1.f, E::Attack));
	TestTrue(TEXT("an attack restarts an attack"), DCUnitAnim::CanStart(E::Attack, 0.1f, 1.f, E::Attack));
	TestFalse(TEXT("None never starts"), DCUnitAnim::CanStart(E::None, 0.f, 0.f, E::None));

	// Envelope: a spawn starts at full weight and settles; a death ends down and stays; a bad row stays in 0..1.
	FDCUnitClip Spawn;
	Spawn.Seconds = 1.f;
	Spawn.RiseFrac = 0.f;
	Spawn.HoldFrac = 0.2f;
	TestEqual(TEXT("spawn starts full"), DCUnitAnim::Envelope(Spawn, 0.f, false), 1.f);
	TestEqual(TEXT("spawn holds"), DCUnitAnim::Envelope(Spawn, 0.15f, false), 1.f);
	TestTrue(TEXT("spawn settles"), DCUnitAnim::Envelope(Spawn, 0.6f, false) < 1.f && DCUnitAnim::Envelope(Spawn, 0.6f, false) > 0.f);
	TestEqual(TEXT("spawn ends at rest"), DCUnitAnim::Envelope(Spawn, 1.2f, false), 0.f);
	FDCUnitClip Death;
	Death.Seconds = 0.8f;
	Death.RiseFrac = 0.7f;
	Death.HoldFrac = 0.3f;
	TestEqual(TEXT("death starts up"), DCUnitAnim::Envelope(Death, 0.f, true), 0.f);
	TestEqual(TEXT("death stays down"), DCUnitAnim::Envelope(Death, 9.f, true), 1.f);
	FDCUnitClip Wild;
	Wild.Seconds = -3.f;
	Wild.RiseFrac = 7.f;
	Wild.HoldFrac = -2.f;
	for (float T : {-1.f, 0.f, 0.005f, 0.5f, 3.f})
	{
		const float W = DCUnitAnim::Envelope(Wild, T, false);
		TestTrue(*FString::Printf(TEXT("a bad row's weight at %.3f stays in 0..1"), T), W >= 0.f && W <= 1.f);
	}

	// Stride: the phase follows distance (one cycle per StrideCm) and wraps.
	FDCUnitGait G;
	G.StrideCm = 100.f;
	TestEqual(TEXT("half a stride is half a cycle"), DCUnitAnim::AdvancePhase(0.f, 50.f, G), 0.5f);
	TestTrue(TEXT("the phase wraps"), FMath::IsNearlyEqual(DCUnitAnim::AdvancePhase(0.9f, 30.f, G), 0.2f, 1e-4f));
	TestEqual(TEXT("moving back never runs the phase backwards"), DCUnitAnim::AdvancePhase(0.3f, -50.f, G), 0.3f);

	// LOD: nearest row first, the last row covers the rest, a default without rows.
	TArray<FDCAnimLod> Lods;
	Lods.AddDefaulted(2);
	Lods[0].MaxDistCm = 1000.f;
	Lods[0].Hz = 0.f;
	Lods[1].MaxDistCm = 5000.f;
	Lods[1].Hz = 10.f;
	Lods[1].bLimbs = false;
	TestEqual(TEXT("near row"), DCUnitAnim::Lod(Lods, 500.f).Hz, 0.f);
	TestEqual(TEXT("mid row"), DCUnitAnim::Lod(Lods, 3000.f).Hz, 10.f);
	TestFalse(TEXT("far rows past the end use the last"), DCUnitAnim::Lod(Lods, 1e7f).bLimbs);
	TestTrue(TEXT("no rows: every frame"), DCUnitAnim::Lod(TArray<FDCAnimLod>(), 10.f).bLimbs);

	// Pose: standing still the legs rest; walking, the legs swing opposite; the run leans forward.
	G.RunSpeedCm = 400.f;
	G.LeanDeg = 10.f;
	FDCLimbSwing L;
	L.Role = TEXT("LegL");
	L.SwingDeg = 30.f;
	G.Limbs.Add(L);
	L.Role = TEXT("LegR");
	L.Phase = 0.5f;
	G.Limbs.Add(L);
	const TArray<FName> Roles = {TEXT("LegL"), TEXT("LegR"), TEXT("Tail")};
	FDCUnitAnimInput In;
	In.Phase = 0.25f;
	FDCUnitPose Pose;
	DCUnitAnim::Pose(G, nullptr, In, Roles, Pose);
	TestEqual(TEXT("one rotation per segment"), Pose.Segments.Num(), 3);
	TestTrue(TEXT("still: legs at rest"), Pose.Segments[0].Equals(FQuat::Identity, 1e-4f));
	In.SpeedCm = 400.f;
	DCUnitAnim::Pose(G, nullptr, In, Roles, Pose);
	const float LeftPitch = Pose.Segments[0].Rotator().Pitch;
	const float RightPitch = Pose.Segments[1].Rotator().Pitch;
	TestTrue(TEXT("running: the left leg swings forward a full swing"), FMath::IsNearlyEqual(LeftPitch, 30.f, 0.1f));
	TestTrue(TEXT("running: the right leg swings back"), FMath::IsNearlyEqual(RightPitch, -30.f, 0.1f));
	TestTrue(TEXT("a role without a limb row stays put"), Pose.Segments[2].Equals(FQuat::Identity, 1e-4f));
	TestTrue(TEXT("the run leans forward"), FMath::IsNearlyEqual(Pose.RootRot.Pitch, -10.f, 0.01f));
	// A clip on top: the death clip's root move at full weight, the walk gone (LocoKeep 0).
	In.Clip = E::Death;
	In.ClipTime = 5.f;
	FDCUnitClip Fall = Death;
	Fall.LocoKeep = 0.f;
	Fall.RootRot = FRotator(-80.f, 0.f, 0.f);
	DCUnitAnim::Pose(G, &Fall, In, Roles, Pose);
	TestTrue(TEXT("dead: fallen forward"), FMath::IsNearlyEqual(Pose.RootRot.Pitch, -80.f, 0.01f));
	TestTrue(TEXT("dead: the legs stop walking"), Pose.Segments[0].Equals(FQuat::Identity, 1e-4f));
	// Far LOD: limbs hold their rest pose.
	In.Clip = E::None;
	In.bLimbs = false;
	DCUnitAnim::Pose(G, nullptr, In, Roles, Pose);
	TestTrue(TEXT("far: limbs hold"), Pose.Segments[0].Equals(FQuat::Identity, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCUnitAnimDataTest, "Epic_AI_Mega_Jam2.UnitAnim.Data", AnimFlags)
bool FDCUnitAnimDataTest::RunTest(const FString&)
{
	// The shipped rows: every gait a unit names exists, every clip a gait names exists and is sane, every limb a gait
	// moves is a part of each model that walks it, and every unit model with parts stays inside the material's segments.
	const UDCTuning& T = UDCTuning::Get();
	TestTrue(TEXT("gaits shipped"), T.UnitGaits.Num() >= 5);
	TestTrue(TEXT("LOD rows shipped"), T.AnimLods.Num() >= 2);
	for (int32 i = 1; i < T.AnimLods.Num(); ++i)
	{
		TestTrue(TEXT("LOD rows nearest first"), T.AnimLods[i].MaxDistCm > T.AnimLods[i - 1].MaxDistCm);
	}
	for (const FDCUnitGait& G : T.UnitGaits)
	{
		const FString Id = G.Id.ToString();
		TestTrue(*FString::Printf(TEXT("gait %s stride is sane"), *Id), G.StrideCm >= 20.f && G.StrideCm <= 400.f);
		for (EDCUnitClip C : {EDCUnitClip::Spawn, EDCUnitClip::Attack, EDCUnitClip::Flinch, EDCUnitClip::Death})
		{
			const FName ClipId = DCUnitAnim::ClipId(G, C);
			const FDCUnitClip* Row = DCUnitAnim::FindClip(T.UnitClips, ClipId);
			TestNotNull(*FString::Printf(TEXT("gait %s clip %s exists"), *Id, *ClipId.ToString()), Row);
			if (!Row)
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("clip %s length 0.1..3 s"), *ClipId.ToString()), Row->Seconds >= 0.1f && Row->Seconds <= 3.f);
			TestTrue(*FString::Printf(TEXT("clip %s envelope fits"), *ClipId.ToString()), Row->RiseFrac >= 0.f && Row->HoldFrac >= 0.f && Row->RiseFrac + Row->HoldFrac <= 1.001f);
		}
		const FDCUnitClip* SpawnRow = DCUnitAnim::FindClip(T.UnitClips, G.Spawn);
		const FDCUnitClip* DeathRow = DCUnitAnim::FindClip(T.UnitClips, G.Death);
		TestTrue(*FString::Printf(TEXT("gait %s spawn starts in its move (RiseFrac 0)"), *Id), SpawnRow && SpawnRow->RiseFrac == 0.f);
		TestTrue(*FString::Printf(TEXT("gait %s death ends down (RiseFrac + HoldFrac = 1)"), *Id), DeathRow && FMath::IsNearlyEqual(DeathRow->RiseFrac + DeathRow->HoldFrac, 1.f, 1e-3f));
	}
	int32 Animated = 0;
	for (const FDCEnemyDef& E : T.Enemies)
	{
		if (E.Gait.IsNone())
		{
			continue;
		}
		++Animated;
		const FDCUnitGait* G = DCUnitAnim::FindGait(T.UnitGaits, E.Gait);
		TestNotNull(*FString::Printf(TEXT("unit %s gait %s exists"), *E.Id.ToString(), *E.Gait.ToString()), G);
		const FDCVoxelModel* M = DCVoxelModels::Parsed(E.Model);
		TestNotNull(*FString::Printf(TEXT("unit %s model %s loads"), *E.Id.ToString(), *E.Model.ToString()), M);
		if (!G || !M)
		{
			continue;
		}
		const TArray<FName> Roles = M->SegmentRoles();
		TestTrue(*FString::Printf(TEXT("unit %s model has animation parts"), *E.Id.ToString()), Roles.Num() >= 2);
		TestTrue(*FString::Printf(TEXT("unit %s model fits the material's segments"), *E.Id.ToString()), Roles.Num() <= FDCVoxelModel::MaxSegments);
		for (const FDCLimbSwing& L : G->Limbs)
		{
			TestTrue(*FString::Printf(TEXT("gait %s limb %s is a part of %s"), *G->Id.ToString(), *L.Role.ToString(), *E.Model.ToString()), Roles.Contains(L.Role));
		}
	}
	TestTrue(TEXT("every faction's infantry is animated"), Animated >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDCUnitAnimMeshTest, "Epic_AI_Mega_Jam2.UnitAnim.SegmentedMesh", AnimFlags)
bool FDCUnitAnimMeshTest::RunTest(const FString&)
{
	// A two-voxel model, foot and body: the foot is a part with its joint on top; the mesh tags its vertices.
	const FString Text = TEXT("dcvox 1\nname t\ngrid_cm 5\nsize 1 1 2\npivot 0 0 0\nslots Ink\npart LegL 0 0 0 1 1 1 0.5 0.5 1\nrle\n2*1\n");
	FDCVoxelModel M;
	FString Error;
	TestTrue(*FString::Printf(TEXT("a model with a part parses: %s"), *Error), FDCVoxelModel::Parse(Text, M, Error));
	TestEqual(TEXT("one part"), M.Parts.Num(), 1);
	TestEqual(TEXT("the lower voxel is the part"), M.PartOf(FIntVector(0, 0, 0)), 0);
	TestEqual(TEXT("the upper voxel is the body"), M.PartOf(FIntVector(0, 0, 1)), INDEX_NONE);
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	TArray<FName> Names;
	for (const FDCVoxelMaterial& P : V.Palette)
	{
		Names.Add(P.Id);
	}
	FDCVoxelUnitMesh Unit;
	M.BuildUnitMesh(Names, V.MeshColors(), Unit);
	TestEqual(TEXT("UV1 per vertex"), Unit.UV1.Num(), Unit.Mesh.Positions.Num());
	TestEqual(TEXT("UV2 per vertex"), Unit.UV2.Num(), Unit.Mesh.Positions.Num());
	TestEqual(TEXT("two cubes, each capped at the joint: 12 quads"), Unit.Mesh.Indices.Num() / 6, 12);
	int32 Foot = 0;
	int32 Body = 0;
	bool bFootTagged = true;
	bool bBodyRoot = true;
	for (int32 i = 0; i < Unit.Mesh.Positions.Num() && i < Unit.UV1.Num(); ++i)
	{
		const float Z = Unit.Mesh.Positions[i].Z;
		const bool bFoot = Unit.UV1[i].X == 1.f;
		Foot += bFoot ? 1 : 0;
		Body += bFoot ? 0 : 1;
		bFootTagged &= !bFoot || (Z <= 5.f + KINDA_SMALL_NUMBER && FMath::IsNearlyEqual(Unit.UV1[i].Y, 2.5f) && FMath::IsNearlyEqual(Unit.UV2[i].X, 2.5f) && FMath::IsNearlyEqual(Unit.UV2[i].Y, 5.f));
		bBodyRoot &= bFoot || (Unit.UV1[i].X == 0.f && Z >= 5.f - KINDA_SMALL_NUMBER);
	}
	TestTrue(TEXT("both segments have vertices"), Foot > 0 && Body > 0);
	TestTrue(TEXT("foot vertices carry segment 1 and the joint (2.5, 2.5, 5) cm"), bFootTagged);
	TestTrue(TEXT("body vertices are the root segment"), bBodyRoot);
	TestFalse(TEXT("a part line with too few numbers is rejected"), FDCVoxelModel::Parse(TEXT("dcvox 1\nsize 1 1 1\nslots Ink\npart LegL 0 0 0 1 1 1\nrle\n1*1\n"), M, Error));
	FString Many = TEXT("dcvox 1\nsize 1 1 1\nslots Ink\n");
	for (int32 i = 0; i < 9; ++i)
	{
		Many += FString::Printf(TEXT("part R%d 0 0 0 1 1 1 0 0 0\n"), i);
	}
	TestFalse(TEXT("more roles than the material reads are rejected"), FDCVoxelModel::Parse(Many + TEXT("rle\n1*1\n"), M, Error));
	return true;
}

#endif
