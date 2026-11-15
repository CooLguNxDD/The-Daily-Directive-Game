// R24 weapon chips on the city (human 2026-11-15, advisor a-37): the host turns weapon hits into Chip ops through its
// chip ledger (Rules/DCChipRules); every machine applies them like any op (DCVoxelOps kind registry). Kept out of
// DCVoxelCity.cpp, which owns the op queue, uploads and commit timing.
#include "DCVoxelCity.h"

#include "DCBuilding.h"
#include "DCEnvironment.h"
#include "DCGameMode.h"
#include "DCGameState.h"
#include "DCTuning.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	/** Tuning ChipsEnabled, or -dcchips on the host (evidence runs while the data default is off). */
	bool ChipsOn(const FDCVoxelTuning& Tn)
	{
		static const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("dcchips"));
		return Tn.ChipsEnabled || bForce;
	}
}

int32 ADCVoxelCity::ChipGridOf(const UPrimitiveComponent* Comp) const
{
	// Cell meshes carry their grid in a tag (ApplyCell): "Terrain", or "Lot<k>" with k = the tower lot, or the tower
	// count for the prop grid.
	if (!Comp || Comp->GetOwner() != this)
	{
		return INDEX_NONE;
	}
	for (const FName& Tag : Comp->ComponentTags)
	{
		const FString S = Tag.ToString();
		if (!S.StartsWith(TEXT("Lot")))
		{
			continue;
		}
		const int32 K = FCString::Atoi(*S.RightChop(3));
		if (Sector.Towers.IsValidIndex(K))
		{
			return K;
		}
		return K == Sector.Towers.Num() ? DCVoxelOps::PropGrid : INDEX_NONE;
	}
	return INDEX_NONE;
}

void ADCVoxelCity::HostChip(const FHitResult& Hit, const FDCWeaponDef& Weapon, const AActor* Shooter)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (!ChipsOn(Tn) || Weapon.ChipDamage <= 0.f || !HasAuthority() || GetNetMode() == NM_Client || BuiltSector == INDEX_NONE || !bHostRests)
	{
		return;
	}
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const int32 Grid = ChipGridOf(Hit.GetComponent());
	if (!GS || Grid == INDEX_NONE)
	{
		return;
	}
	if (!DCChipRules::TakeToken(ChipLedger, PointerHash(Shooter), Weapon.ChipsPerSecond, GS->Now()))
	{
		return;
	}
	// The voxel behind the hit face (half a voxel in along the normal). Grid origin and size never change while an op
	// runs, so this reads no voxels and is safe beside the worker.
	const FDCVoxelGrid& G = Grid == DCVoxelOps::PropGrid ? Sector.Props : Sector.Towers[Grid].Grid;
	const FVector Inside = Hit.ImpactPoint - Hit.ImpactNormal * (G.VoxelCm * 0.5f);
	const FIntVector Center = G.CenterCm(G.ToLocal(DCVoxelMath::ToCm(Inside)));
	const float Hp = Grid == DCVoxelOps::PropGrid ? Tn.ChipPropHp : Tn.ChipHp;
	DCChipRules::AddHit(ChipLedger, Grid, Center, Tn.ChipBucketCm, Weapon.ChipDamage, Hp, Weapon.ChipRadiusCm, Tn.ChipMaxPending);
}

void ADCVoxelCity::SetChipProps(const FDCCityPlan& Plan)
{
	// Every plan prop with a model: foot at its location, reach = the farthest model corner from the pivot (any quarter
	// turn), top = its height above the pivot, broken past its layer's ChipBreakPercent of its voxels.
	const UDCVoxelSettings& V = UDCVoxelSettings::Get();
	const int32 Vcm = FMath::Max(1, V.Tuning.PropVoxelCm);
	ChipLedger.Props.Reset();
	ChipLedger.BreakDue.Reset();
	for (const FDCPropPlan& P : Plan.Props)
	{
		const FDCVoxelModel* M = DCVoxelModels::Parsed(P.Model);
		const FDCPropLayer* Layer = V.PropLayers.FindByPredicate([&P](const FDCPropLayer& L) { return L.Id == P.Layer; });
		if (!M)
		{
			continue;
		}
		int32 Solid = 0;
		for (const uint8 C : M->Cells)
		{
			Solid += C != 0 ? 1 : 0;
		}
		// Reach: the model's larger half extent from the pivot (not the diagonal: the break column would cut its
		// neighbours); a prop wider than a chip column can clear never breaks (no floating remains).
		const int32 Reach = FMath::Max(FMath::Max(M->Pivot.X, M->Size.X - M->Pivot.X), FMath::Max(M->Pivot.Y, M->Size.Y - M->Pivot.Y)) * Vcm;
		FDCChipProp& Out = ChipLedger.Props.AddDefaulted_GetRef();
		Out.FootCm = DCVoxelMath::ToCm(P.Location);
		Out.RadiusCm = Reach;
		Out.TopCm = Out.FootCm.Z + (M->Size.Z - M->Pivot.Z) * Vcm;
		const bool bBreaks = Layer && Layer->ChipBreakPercent > 0 && Reach <= DCVoxelOps::ChipMaxRadiusCm;
		Out.BreakAt = bBreaks ? FMath::Max(1, Solid * Layer->ChipBreakPercent / 100) : 0;
	}
}

void ADCVoxelCity::CheckChipHash()
{
	const ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	const double Real = FPlatformTime::Seconds();
	if (GetNetMode() != NM_Client || !bFollowGameState || !GS || GS->VoxelOps.Num() == 0 || Real < ChipCheckNext || !IsOpQueueIdle() || bLoading)
	{
		return;
	}
	const FDCVoxelOp& Last = GS->VoxelOps.Last();
	if (Last.Build != BuiltId || Last.Seq != AppliedSeq || Last.Seq == ChipCheckedSeq || Last.ResultHash == 0 || !DCVoxelOps::IsChipOnly(Last))
	{
		return;
	}
	ChipCheckedSeq = Last.Seq;
	ChipCheckNext = Real + 1.0; // at most once a second (a whole-city hash, cached per chunk)
	const uint32 Mine = Sector.Hash();
	UE_LOG(LogTemp, Display, TEXT("DCCHIP check seq=%d chips=%d hash=%08x host=%08x %s"), Last.Seq, Last.Chips.Num(), Mine, Last.ResultHash, Mine == Last.ResultHash ? TEXT("match=host") : TEXT("MISMATCH=host"));
}

void ADCVoxelCity::TickChips()
{
	CheckChipHash();
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	if (!ChipsOn(Tn) || !HasAuthority() || GetNetMode() == NM_Client || BuiltSector == INDEX_NONE || bLoading || !bHostRests)
	{
		return;
	}
	ADCGameState* GS = GetWorld()->GetGameState<ADCGameState>();
	if (!GS || !GS->HasAuthority() || GS->Phase != EDCRunPhase::Sector || GS->CityBuildId != BuiltId)
	{
		return;
	}
	FDCVoxelOp Op;
	if (!DCChipRules::Flush(ChipLedger, GS->Now(), FDCChipParams::From(Tn), bChipOpOut, BodyLoad(), Op))
	{
		return;
	}
	// A structure pass commits like a strike (two-phase: planned ahead, the topple shows in one frame); plain chips
	// commit as soon as they are meshed, under the upload budget.
	const bool bStructure = Op.StructureLots.Num() > 0;
	// A plain chip op merges into the last history entry when that is one too (MergeInto): the host applies only the
	// new chips; clients apply the merged entry (old chips clear nothing twice).
	Op.Build = GS->CityBuildId;
	Op.Sector = GS->CitySector;
	Op.Time = GS->Now();
	const bool bMerged = !bStructure && GS->VoxelOps.Num() > 0 && DCChipRules::MergeInto(GS->VoxelOps.Last(), Op, FMath::Max(1, Tn.ChipMergeMaxChips));
	if (bMerged)
	{
		Op.Seq = GS->TakeVoxelSeq();
		GS->VoxelOps.Last().Seq = Op.Seq;
	}
	else
	{
		Op = GS->AddVoxelOp(Op, bStructure ? GS->Now() + Tn.PlanLeadSeconds : -1.f);
	}
	const int32 Seq = Op.Seq;
	const float OpTime = Op.Time;
	const FVector From(Op.CenterCm);
	bChipOpOut = true;
	UE_LOG(LogTemp, Display, TEXT("DCCHIP host op=%d chips=%d structure=%d breakprops=%d pending=%d merged=%d entry=%d history=%d"), Seq, Op.Chips.Num(), bStructure ? Op.StructureLots[0] : -1,
		Op.BreakProps.Num(), ChipLedger.Pending.Num(), bMerged ? 1 : 0, GS->VoxelOps.Last().Chips.Num(), GS->VoxelOps.Num());
	TWeakObjectPtr<ADCGameState> WeakGS(GS);
	TWeakObjectPtr<ADCVoxelCity> WeakCity(this);
	const int32 Build = BuiltId;
	ApplyOpAsync(Op, true, [WeakGS, WeakCity, Seq, Build, From, OpTime, Chips = Op.Chips](const FDCVoxelOpResult& Result, uint32 Hash)
	{
		ADCGameState* State = WeakGS.Get();
		ADCVoxelCity* City = WeakCity.Get();
		if (!State || !City || City->BuiltId != Build)
		{
			return;
		}
		City->bChipOpOut = false;
		for (const FIntPoint& C : Result.ChipCarved)
		{
			DCChipRules::Credit(City->ChipLedger, C.X, C.Y);
		}
		for (int32 i = 0; i < Chips.Num() && i < Result.ChipVoxels.Num(); ++i)
		{
			if (Chips[i].Grid == DCVoxelOps::PropGrid && Result.ChipVoxels[i] > 0)
			{
				DCChipRules::CreditProp(City->ChipLedger, DCChipRules::PropAt(City->ChipLedger, Chips[i].CenterCm), Result.ChipVoxels[i]);
			}
		}
		if (FDCVoxelOp* Rec = State->VoxelOps.FindByPredicate([Seq](const FDCVoxelOp& O) { return O.Seq == Seq; }))
		{
			Rec->ResultHash = Hash;
		}
		// A tower the shots brought down is a collapse like a strike's (scoring, the reel, the domino rule).
		ADCGameMode* GM = State->GetWorld()->GetAuthGameMode<ADCGameMode>();
		for (ADCBuilding* Building : TActorRange<ADCBuilding>(State->GetWorld()))
		{
			if (GM && Result.FallenLots.Contains(Building->GetLotIndex()))
			{
				UE_LOG(LogTemp, Display, TEXT("DCCHIP op=%d brought lot %d down"), Seq, Building->GetLotIndex());
				GM->ScheduleCollapse(Building, From, TEXT("Strike"), 0.f, OpTime);
			}
		}
	});
}

void ADCVoxelCity::QueueChipCrumbs(const FDCVoxelOp& Op, const FDCVoxelOpResult& Result, float Start)
{
	// Each chip throws a few crumbs of the voxel it removed (rubble colour unless lit), out of the wall: grit, never a
	// physics body (chips are frequent; the shard bodies stay for strikes). Cosmetic and local.
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	const int32 PerChip = FMath::RoundToInt32(Tn.ChipCrumbs * FMath::Clamp(1.f - Tn.CrumbLoadCut * BodyLoad(), 0.f, 1.f) * ADCEnvironment::EffectScaleOf(this));
	if (Result.ShardClusters.Num() == 0)
	{
		return;
	}
	FDCShardBurst& Burst = PendingShards.AddDefaulted_GetRef();
	Burst.Start = Start;
	FRandomStream Rng(static_cast<int32>(DCVoxelOps::Hash(Op.CenterCm, static_cast<uint32>(Op.Seq) ^ 0xC41Bu)));
	for (const FDCShardCluster& C : Result.ShardClusters)
	{
		const bool bLit = Colors.IsValidIndex(C.Material) && Colors[C.Material].A > 0;
		const uint8 Rub = !bLit && OpCtx.RubbleOf.IsValidIndex(C.Material) && OpCtx.RubbleOf[C.Material] != 0 ? OpCtx.RubbleOf[C.Material] : C.Material;
		const FColor Col = Colors.IsValidIndex(Rub) ? Colors[Rub] : FColor(30, 30, 40, 0);
		if (C.bPiece)
		{
			// A broken prop's piece: one pooled debris block about its slice's size, knocked up and out (it flakes into
			// crumbs at the end of its life, R15).
			const float EdgeCm = C.VoxelCm * FMath::Clamp(FMath::Pow(static_cast<float>(C.Count), 1.f / 3.f), 1.f, 12.f);
			FDCDebrisSpawn D;
			D.Transform = FTransform(FRotator(Rng.FRandRange(0, 360), Rng.FRandRange(0, 360), 0), C.Pos, FVector(EdgeCm / 100.f));
			D.LinearVelocity = Rng.VRand() * Tn.ShardSpeedCms * 0.35f + FVector(0, 0, Rng.FRandRange(Tn.ShardUpCms * 0.3f, Tn.ShardUpCms * 0.8f));
			D.AngularVelocityDeg = Rng.VRand() * 360.f;
			D.Color = FLinearColor::FromSRGBColor(FColor(Col.R, Col.G, Col.B, 255));
			D.Glow = Col.A / 255.f * Tn.VoxelMaterialGlowScale * Tn.ShardGlowScale;
			D.LifeSeconds = Tn.ShardLifeSeconds * Rng.FRandRange(0.7f, 1.1f);
			Burst.Spawns.Add(D);
		}
		for (int32 i = 0; i < PerChip * (C.bPiece ? 3 : 1); ++i)
		{
			FDCCrumbSpawn& Cr = Burst.Crumbs.AddDefaulted_GetRef();
			Cr.At = C.Pos + Rng.VRand() * C.VoxelCm;
			Cr.Velocity = Rng.VRand() * Tn.CrumbSpeedCms * Rng.FRandRange(0.2f, 0.6f) + FVector(0, 0, Rng.FRandRange(50.f, Tn.CrumbUpCms));
			Cr.SizeCm = C.VoxelCm * Rng.RandRange(1, 2);
			Cr.Color = FLinearColor::FromSRGBColor(FColor(Col.R, Col.G, Col.B, 255));
			Cr.Glow = Col.A / 255.f * Tn.VoxelMaterialGlowScale * Tn.CrumbGlowScale;
		}
	}
}
