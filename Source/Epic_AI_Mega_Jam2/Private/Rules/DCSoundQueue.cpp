#include "Rules/DCSoundQueue.h"

namespace DCSoundQueue
{
	int32 ClassOf(const TArray<FDCSoundClassDef>& Classes, FName Cue)
	{
		int32 Default = INDEX_NONE;
		for (int32 i = 0; i < Classes.Num(); ++i)
		{
			if (Classes[i].Cues.Contains(Cue))
			{
				return i;
			}
			Default = Classes[i].Id == TEXT("Default") ? i : Default;
		}
		return Default;
	}

	namespace
	{
		bool Near(const FDCSoundClassDef& C, bool b2DA, const FVector& A, bool b2DB, const FVector& B)
		{
			return b2DA == b2DB && (b2DA || FVector::DistSquared(A, B) <= FMath::Square(C.MergeRadiusCm));
		}

		float Boosted(const FDCSoundClassDef& C, float Kept, float Added)
		{
			return FMath::Min(FMath::Max(Kept, Added) + C.MergeVolumeBoost * FMath::Min(Kept, Added), C.MaxVolume);
		}
	}

	void Flush(const TArray<FDCSoundClassDef>& Classes, const FDCSoundMixTuning& Mix, double Now,
		TFunctionRef<float(FName)> CueSeconds, TArray<FDCSoundRequest>& Pending, TArray<FDCSoundVoice>& Voices,
		TArray<FDCSoundRequest>& OutPlay, TArray<FDCSoundDropped>& OutDrop)
	{
		Voices.RemoveAll([Now](const FDCSoundVoice& V) { return V.EndTime <= Now; });

		// 1. Merge: into an earlier request of this flush, or drop when the same cue just started near the same spot.
		TArray<FDCSoundRequest> Work;
		Work.Reserve(Pending.Num());
		for (const FDCSoundRequest& R : Pending)
		{
			const int32 Ci = ClassOf(Classes, R.Cue);
			if (Ci == INDEX_NONE)
			{
				Work.Add(R);
				continue;
			}
			const FDCSoundClassDef& C = Classes[Ci];
			FDCSoundRequest* Into = Work.FindByPredicate([&](const FDCSoundRequest& W) { return W.Cue == R.Cue && Near(C, W.b2D, W.Location, R.b2D, R.Location); });
			if (Into)
			{
				Into->Volume = Boosted(C, Into->Volume, R.Volume);
				Into->Merged += 1 + R.Merged;
				continue;
			}
			const bool bJustPlayed = R.WaitedFrames == 0 && Voices.ContainsByPredicate([&](const FDCSoundVoice& V)
			{
				return V.Cue == R.Cue && Now - V.StartTime <= C.MergeWindowSeconds && Near(C, V.b2D, V.Location, R.b2D, R.Location);
			});
			if (bJustPlayed)
			{
				OutDrop.Add({R, EDCSoundDrop::Merged});
				continue;
			}
			Work.Add(R);
		}

		// 2. Highest class priority first; within a class, the oldest request first (stable).
		auto Prio = [&Classes](const FDCSoundRequest& R)
		{
			const int32 Ci = ClassOf(Classes, R.Cue);
			return Ci == INDEX_NONE ? 0 : Classes[Ci].Priority;
		};
		Work.StableSort([&](const FDCSoundRequest& A, const FDCSoundRequest& B) { return Prio(A) > Prio(B) || (Prio(A) == Prio(B) && A.WaitedFrames > B.WaitedFrames); });

		// 3. Budgets: class voices, class starts this frame, the shared cap for unprotected classes.
		TArray<int32> Live, Started;
		Live.Init(0, Classes.Num());
		Started.Init(0, Classes.Num());
		int32 Shared = 0;
		for (const FDCSoundVoice& V : Voices)
		{
			if (Classes.IsValidIndex(V.Class))
			{
				++Live[V.Class];
				Shared += Classes[V.Class].Priority < Mix.ProtectedPriority ? 1 : 0;
			}
		}
		Pending.Reset();
		for (FDCSoundRequest R : Work)
		{
			const int32 Ci = ClassOf(Classes, R.Cue);
			const float Seconds = FMath::Max(CueSeconds(R.Cue), 0.05f);
			if (Ci == INDEX_NONE)
			{
				OutPlay.Add(R);
				continue;
			}
			const FDCSoundClassDef& C = Classes[Ci];
			const bool bProtected = C.Priority >= Mix.ProtectedPriority;
			const bool bFits = Live[Ci] < C.MaxVoices && Started[Ci] < C.StartsPerFrame && (bProtected || Shared < Mix.TotalVoices);
			if (!bFits)
			{
				++R.WaitedFrames;
				if (R.WaitedFrames > C.MaxWaitFrames)
				{
					OutDrop.Add({R, EDCSoundDrop::Budget});
				}
				else
				{
					Pending.Add(R);
				}
				continue;
			}
			R.Pitch = FMath::Clamp(R.Pitch, C.PitchMin, C.PitchMax);
			R.Volume = FMath::Min(R.Volume * C.Volume, C.MaxVolume);
			++Live[Ci];
			++Started[Ci];
			Shared += bProtected ? 0 : 1;
			FDCSoundVoice V;
			V.Cue = R.Cue;
			V.Class = Ci;
			V.Location = R.Location;
			V.b2D = R.b2D;
			V.StartTime = Now;
			V.EndTime = Now + Seconds / FMath::Max(R.Pitch, 0.1f);
			Voices.Add(V);
			OutPlay.Add(R);
		}
	}
}
