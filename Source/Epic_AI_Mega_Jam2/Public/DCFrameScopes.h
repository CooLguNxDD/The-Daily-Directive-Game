// R1 frame time (advisor a-33): named game-thread timers, summed per frame, so a stall names its owner. A site wraps
// itself with DC_FRAME_SCOPE("Name"); the names are the registry (one per site, no central list to keep in sync).
// The table rotates at the start of every engine frame, so LastFrame() is the whole previous frame, the same frame
// GGameThreadTime and FApp::GetDeltaTime describe. Off (one static bool test) unless the run passes -dcperf.
#pragma once

#include "CoreMinimal.h"

namespace DCFrameScopes
{
	struct FTotal
	{
		FName Name;
		double Ms = 0.0;
		int32 Calls = 0;
		/** Not inside another of our scopes: the sum of these is the time we own. */
		bool bTop = true;
	};

	EPIC_AI_MEGA_JAM2_API bool Enabled();
	/** The previous frame's totals, largest first (engine phases World and GC included, flagged by name). */
	EPIC_AI_MEGA_JAM2_API const TArray<FTotal>& LastFrame();
	/** "owners=A:12.0x3,B:4.1 ours=16.1 world=20.3 gc=0.0 unowned=3.9" for a spike line; GameMs is the frame's game thread. */
	EPIC_AI_MEGA_JAM2_API FString Describe(int32 TopN, double GameMs);
	EPIC_AI_MEGA_JAM2_API void Add(FName Name, double Ms, bool bTop);

	class EPIC_AI_MEGA_JAM2_API FScope
	{
	public:
		explicit FScope(FName InName);
		~FScope();
		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;

	private:
		FName Name;
		double T0 = 0.0;
		bool bOn = false;
		bool bTop = false;
	};
}

#define DC_FRAME_SCOPE(Label) \
	static const FName PREPROCESSOR_JOIN(DCFrameScopeName_, __LINE__)(TEXT(Label)); \
	DCFrameScopes::FScope PREPROCESSOR_JOIN(DCFrameScope_, __LINE__)(PREPROCESSOR_JOIN(DCFrameScopeName_, __LINE__))
