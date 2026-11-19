#include "Rules/DCCueMix.h"

namespace DCCueMix
{
	const FDCCueMix* Find(const TArray<FDCCueMix>& Rows, FName Cue)
	{
		return Rows.FindByPredicate([Cue](const FDCCueMix& R) { return R.Cue == Cue; });
	}

	float Trim(const TArray<FDCCueMix>& Rows, FName Cue)
	{
		const FDCCueMix* Row = Find(Rows, Cue);
		return Row ? FMath::Clamp(Row->Volume, 0.f, 4.f) : 1.f;
	}
}
