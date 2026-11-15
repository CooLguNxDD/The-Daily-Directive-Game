#include "Rules/DCChipRules.h"

#include "Voxel/DCVoxelOps.h"

FDCChipParams FDCChipParams::From(const FDCVoxelTuning& Tuning)
{
	FDCChipParams P;
	P.WindowSeconds = FMath::Max(Tuning.ChipWindowSeconds, 0.f);
	P.MaxEntries = FMath::Max(Tuning.ChipMaxEntries, 1);
	P.MaxCellsPerOp = FMath::Max(Tuning.ChipMaxCellsPerOp, 1);
	P.RenderCellCm = FMath::Max(Tuning.RenderCellCm, 1);
	P.MaxPending = FMath::Max(Tuning.ChipMaxPending, 1);
	P.StructureVoxels = FMath::Max(Tuning.ChipStructureVoxels, 1);
	P.StructureMinSeconds = FMath::Max(Tuning.ChipStructureMinSeconds, 0.f);
	P.StructureMaxBodyLoad = Tuning.ChipStructureMaxBodyLoad;
	return P;
}

void FDCChipLedger::Reset()
{
	Props.Reset();
	BreakDue.Reset();
	Damage.Reset();
	Pending.Reset();
	Tokens.Reset();
	Lots.Reset();
	NextFlush = 0.f;
}

namespace
{
	int32 FloorDiv(int32 A, int32 B)
	{
		return A >= 0 ? A / B : -((-A + B - 1) / B);
	}

	FIntVector Floor3(const FIntVector& Cm, int32 Edge)
	{
		return FIntVector(FloorDiv(Cm.X, Edge), FloorDiv(Cm.Y, Edge), FloorDiv(Cm.Z, Edge));
	}
}

namespace DCChipRules
{
	bool TakeToken(FDCChipLedger& L, uint32 Shooter, float PerSecond, float Now)
	{
		if (PerSecond <= 0.f)
		{
			return false;
		}
		const float Burst = FMath::Max(1.f, PerSecond);
		FVector2f* T = L.Tokens.Find(Shooter);
		if (!T)
		{
			T = &L.Tokens.Add(Shooter, FVector2f(Burst, Now));
		}
		T->X = FMath::Min(Burst, T->X + FMath::Max(Now - T->Y, 0.f) * PerSecond);
		T->Y = Now;
		if (T->X < 1.f)
		{
			return false;
		}
		T->X -= 1.f;
		return true;
	}

	bool AddHit(FDCChipLedger& L, int32 Grid, const FIntVector& VoxelCenterCm, int32 BucketCm, float Damage, float Hp, int32 RadiusCm, int32 MaxPending)
	{
		if (Damage <= 0.f || RadiusCm <= 0)
		{
			return false;
		}
		if (L.Damage.Num() >= FDCChipLedger::MaxBuckets)
		{
			L.Damage.Reset();
		}
		const FIntVector B = Floor3(VoxelCenterCm, FMath::Max(BucketCm, 1));
		const FIntVector4 Key(Grid, B.X, B.Y, B.Z);
		float& D = L.Damage.FindOrAdd(Key);
		D += Damage;
		if (D < Hp)
		{
			return false;
		}
		L.Damage.Remove(Key);
		if (FDCVoxelChip* Same = L.Pending.FindByPredicate([Grid, &VoxelCenterCm](const FDCVoxelChip& C) { return C.Grid == Grid && C.CenterCm == VoxelCenterCm; }))
		{
			Same->RadiusCm = FMath::Max(Same->RadiusCm, RadiusCm);
			return false;
		}
		if (L.Pending.Num() >= MaxPending)
		{
			return false;
		}
		FDCVoxelChip& C = L.Pending.AddDefaulted_GetRef();
		C.Grid = Grid;
		C.CenterCm = VoxelCenterCm;
		C.RadiusCm = RadiusCm;
		return true;
	}

	bool Flush(FDCChipLedger& L, float Now, const FDCChipParams& P, bool bChipOpOut, float BodyLoad, FDCVoxelOp& Out)
	{
		if (bChipOpOut || Now < L.NextFlush)
		{
			return false;
		}
		// The structure lot: the lowest lot over its voxel count, outside its interval, under the body load gate.
		int32 Due = INDEX_NONE;
		if (BodyLoad <= P.StructureMaxBodyLoad)
		{
			for (const TPair<int32, FDCChipLot>& It : L.Lots)
			{
				const bool bReady = It.Value.Voxels >= P.StructureVoxels && Now - It.Value.LastStructure >= P.StructureMinSeconds;
				Due = bReady && (Due == INDEX_NONE || It.Key < Due) ? It.Key : Due;
			}
		}
		if (L.Pending.Num() == 0 && Due == INDEX_NONE && L.BreakDue.Num() == 0)
		{
			return false;
		}
		Out.Kind = TEXT("Chip");
		Out.Chips.Reset();
		Out.StructureLots.Reset();
		Out.BreakProps.Reset();
		while (L.BreakDue.Num() > 0 && Out.BreakProps.Num() < FDCChipLedger::MaxBreaksPerOp)
		{
			const FDCChipProp& Prop = L.Props[L.BreakDue[0]];
			FDCVoxelChip& Col = Out.BreakProps.AddDefaulted_GetRef();
			Col.Grid = DCVoxelOps::PropGrid;
			Col.CenterCm = FIntVector(Prop.FootCm.X, Prop.FootCm.Y, Prop.TopCm);
			Col.RadiusCm = Prop.RadiusCm;
			L.BreakDue.RemoveAt(0);
		}
		Out.Lot = INDEX_NONE;
		// Chips in arrival order, while they fit the entry cap and the render cell budget (a cell already in the op is
		// free); a chip on a new cell past the budget waits for the next window.
		TArray<FIntVector4> Cells;
		TArray<FDCVoxelChip> Rest;
		for (const FDCVoxelChip& C : L.Pending)
		{
			const FIntVector Cell = Floor3(C.CenterCm, P.RenderCellCm);
			const FIntVector4 Key(C.Grid, Cell.X, Cell.Y, Cell.Z);
			const bool bNewCell = !Cells.Contains(Key);
			if (Out.Chips.Num() >= P.MaxEntries || (bNewCell && Cells.Num() >= P.MaxCellsPerOp))
			{
				Rest.Add(C);
				continue;
			}
			if (bNewCell)
			{
				Cells.Add(Key);
			}
			Out.Chips.Add(C);
			if (C.Grid >= 0)
			{
				FDCChipLot& Lot = L.Lots.FindOrAdd(C.Grid);
				Lot.SumCm += C.CenterCm;
				++Lot.Count;
			}
		}
		L.Pending = MoveTemp(Rest);
		if (Due != INDEX_NONE)
		{
			FDCChipLot& Lot = L.Lots[Due];
			Out.StructureLots.Add(Due);
			Out.Lot = Due;
			Out.CenterCm = Lot.Count > 0 ? FIntVector(Lot.SumCm.X / Lot.Count, Lot.SumCm.Y / Lot.Count, Lot.SumCm.Z / Lot.Count) : FIntVector::ZeroValue;
			Lot.Voxels = 0;
			Lot.SumCm = FIntVector::ZeroValue;
			Lot.Count = 0;
			Lot.LastStructure = Now;
		}
		else if (Out.Chips.Num() > 0)
		{
			Out.CenterCm = Out.Chips[0].CenterCm;
		}
		else if (Out.BreakProps.Num() > 0)
		{
			Out.CenterCm = Out.BreakProps[0].CenterCm;
		}
		L.NextFlush = Now + P.WindowSeconds;
		return true;
	}

	void Credit(FDCChipLedger& L, int32 Lot, int32 Voxels)
	{
		if (Lot >= 0 && Voxels > 0)
		{
			L.Lots.FindOrAdd(Lot).Voxels += Voxels;
		}
	}

	int32 PropAt(const FDCChipLedger& L, const FIntVector& Cm, int32 SlackCm)
	{
		int32 Best = INDEX_NONE;
		int64 BestD = MAX_int64;
		for (int32 i = 0; i < L.Props.Num(); ++i)
		{
			const FDCChipProp& P = L.Props[i];
			const int64 Dx = Cm.X - P.FootCm.X, Dy = Cm.Y - P.FootCm.Y;
			const int64 D = Dx * Dx + Dy * Dy;
			const int64 Reach = P.RadiusCm + SlackCm;
			if (!P.bBroken && D <= Reach * Reach && Cm.Z <= P.TopCm + SlackCm && D < BestD)
			{
				Best = i;
				BestD = D;
			}
		}
		return Best;
	}

	void CreditProp(FDCChipLedger& L, int32 Index, int32 Voxels)
	{
		if (!L.Props.IsValidIndex(Index) || Voxels <= 0)
		{
			return;
		}
		FDCChipProp& P = L.Props[Index];
		P.Chipped += Voxels;
		if (!P.bBroken && P.BreakAt > 0 && P.Chipped >= P.BreakAt)
		{
			P.bBroken = true;
			L.BreakDue.Add(Index);
		}
	}
}
