#include "DCFrameScopes.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

namespace DCFrameScopes
{
	namespace
	{
		const FName WorldName(TEXT("World"));
		const FName GcName(TEXT("GC"));
		TMap<FName, FTotal> Current;
		TArray<FTotal> Last;
		int32 Depth = 0;
		double WorldT0 = 0.0;
		double GcT0 = 0.0;

		void Rotate()
		{
			Last.Reset();
			for (const TPair<FName, FTotal>& It : Current)
			{
				Last.Add(It.Value);
			}
			Last.Sort([](const FTotal& A, const FTotal& B) { return A.Ms > B.Ms; });
			Current.Reset();
		}

		bool Hook()
		{
			// Engine phases ride the same table: the game world's actor tick, and garbage collection.
			FCoreDelegates::OnBeginFrame.AddStatic(&Rotate);
			FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* World, ELevelTick, float) {
				if (World && World->IsGameWorld())
				{
					WorldT0 = FPlatformTime::Seconds();
				}
			});
			FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* World, ELevelTick, float) {
				if (World && World->IsGameWorld() && WorldT0 > 0.0)
				{
					Add(WorldName, (FPlatformTime::Seconds() - WorldT0) * 1000.0, false);
					WorldT0 = 0.0;
				}
			});
			FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda([] { GcT0 = FPlatformTime::Seconds(); });
			FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([] {
				if (GcT0 > 0.0)
				{
					Add(GcName, (FPlatformTime::Seconds() - GcT0) * 1000.0, false);
					GcT0 = 0.0;
				}
			});
			return true;
		}
	}

	bool Enabled()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("dcperf")) && Hook();
		return bOn;
	}

	const TArray<FTotal>& LastFrame() { return Last; }

	void Add(FName Name, double Ms, bool bTop)
	{
		FTotal& T = Current.FindOrAdd(Name);
		T.Name = Name;
		T.Ms += Ms;
		++T.Calls;
		T.bTop = T.bTop && bTop;
	}

	FString Describe(int32 TopN, double GameMs)
	{
		double Ours = 0.0;
		double World = 0.0;
		double Gc = 0.0;
		FString Owners;
		int32 Listed = 0;
		for (const FTotal& T : Last)
		{
			if (T.Name == WorldName)
			{
				World = T.Ms;
				continue;
			}
			if (T.Name == GcName)
			{
				Gc = T.Ms;
				continue;
			}
			Ours += T.bTop ? T.Ms : 0.0;
			if (Listed < TopN)
			{
				Owners += FString::Printf(TEXT("%s%s:%.1fx%d"), Listed ? TEXT(",") : TEXT(""), *T.Name.ToString(), T.Ms, T.Calls);
				++Listed;
			}
		}
		return FString::Printf(TEXT("owners=%s ours=%.1f world=%.1f gc=%.1f unowned=%.1f outside_world=%.1f"), Owners.IsEmpty() ? TEXT("-") : *Owners, Ours, World, Gc,
			FMath::Max(0.0, GameMs - Ours - Gc), FMath::Max(0.0, GameMs - World));
	}

	FScope::FScope(FName InName)
	{
		if (!Enabled() || !IsInGameThread())
		{
			return;
		}
		Name = InName;
		bOn = true;
		bTop = Depth == 0;
		++Depth;
		T0 = FPlatformTime::Seconds();
	}

	FScope::~FScope()
	{
		if (!bOn)
		{
			return;
		}
		--Depth;
		Add(Name, (FPlatformTime::Seconds() - T0) * 1000.0, bTop);
	}
}
