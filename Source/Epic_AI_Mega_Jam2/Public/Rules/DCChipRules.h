// R24 weapon chips (human 2026-11-15, advisor a-37): every weapon hit wears the voxel it strikes; sustained fire opens
// holes; a lot shot through enough gets the structure pass. Pure host-side ledger (no world, no clock): hits add damage
// to buckets, a full bucket becomes a pending chip, and a due window turns the pending chips into one Chip op. The
// city (ADCVoxelCity::HostChip, DCVoxelCity.cpp) owns one ledger on the host; ops replicate, the ledger never does.
// Tested under -nullrhi (ChipTest.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Voxel/DCVoxelTypes.h"

/** The window and budget numbers (from FDCVoxelTuning Chip* and Tuning RenderCellCm). */
struct EPIC_AI_MEGA_JAM2_API FDCChipParams
{
	float WindowSeconds = 0.25f;
	int32 MaxEntries = 32;
	int32 MaxCellsPerOp = 4;
	int32 RenderCellCm = 960;
	int32 MaxPending = 256;
	int32 StructureVoxels = 300;
	float StructureMinSeconds = 3.f;
	float StructureMaxBodyLoad = 0.6f;

	static FDCChipParams From(const FDCVoxelTuning& Tuning);
};

/** What the host knows about one lot's chips since its last structure pass. */
struct EPIC_AI_MEGA_JAM2_API FDCChipLot
{
	int32 Voxels = 0;
	/** Sum and count of the chip centres sent for this lot (the structure pass topples away from their mean). */
	FIntVector SumCm = FIntVector::ZeroValue;
	int32 Count = 0;
	float LastStructure = -1e9f;
};

struct EPIC_AI_MEGA_JAM2_API FDCChipLedger
{
	/** Damage per (grid, bucket x, y, z); a bucket that chips starts over. Bounded: cleared when it grows past
	 *  MaxBuckets (old wear is forgotten, never memory). */
	TMap<FIntVector4, float> Damage;
	TArray<FDCVoxelChip> Pending;
	/** Rate limit per shooter: (tokens, last refill time). */
	TMap<uint32, FVector2f> Tokens;
	TMap<int32, FDCChipLot> Lots;
	float NextFlush = 0.f;
	static constexpr int32 MaxBuckets = 8192;

	void Reset();
};

namespace DCChipRules
{
	/** Token bucket per shooter: PerSecond hits a second at most, a burst of up to max(1, PerSecond). */
	EPIC_AI_MEGA_JAM2_API bool TakeToken(FDCChipLedger& L, uint32 Shooter, float PerSecond, float Now);

	/** Adds Damage to the bucket (BucketCm cubes of Grid) holding VoxelCenterCm. Past Hp, the voxel is chipped: a chip
	 *  of RadiusCm enters Pending (merged into a pending chip at the same voxel, the larger radius kept) and the bucket
	 *  starts over. Returns true when a chip was added. A full Pending list drops the hit. */
	EPIC_AI_MEGA_JAM2_API bool AddHit(FDCChipLedger& L, int32 Grid, const FIntVector& VoxelCenterCm, int32 BucketCm, float Damage, float Hp, int32 RadiusCm, int32 MaxPending);

	/** A due window (Now >= NextFlush, no Chip op out): moves pending chips in order into Out.Chips, at most MaxEntries
	 *  and on at most MaxCellsPerOp render cells (per grid); the rest stay pending. Lists at most one lot whose shot
	 *  voxels reached StructureVoxels (lowest lot first, StructureMinSeconds apart per lot, only while BodyLoad is
	 *  under StructureMaxBodyLoad) in Out.StructureLots and sets Out.CenterCm to where its chips came from. Returns
	 *  false (Out untouched) when there is nothing to send. */
	EPIC_AI_MEGA_JAM2_API bool Flush(FDCChipLedger& L, float Now, const FDCChipParams& P, bool bChipOpOut, float BodyLoad, FDCVoxelOp& Out);

	/** The host's applied Chip op removed Voxels from Lot (FDCVoxelOpResult::ChipCarved). */
	EPIC_AI_MEGA_JAM2_API void Credit(FDCChipLedger& L, int32 Lot, int32 Voxels);
}
