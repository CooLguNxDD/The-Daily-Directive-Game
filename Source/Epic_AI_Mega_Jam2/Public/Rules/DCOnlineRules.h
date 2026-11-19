// R26 internet co-op, pure rules (no world, no SDK): the local credentials file parser, join codes (make, normalize)
// and the state table of the online flow. Tested under -nullrhi (OnlineTest.cpp).
#pragma once

#include "CoreMinimal.h"
#include "DCOnlineTransport.h"

/** Where the online flow stands. */
enum class EDCOnlineState : uint8
{
	Off,       // nothing started (solo, LAN, gate)
	LoggingIn,
	Ready,     // logged in, transport idle
	Hosting,   // listen map up, session advertised
	Joining,   // looking the code up / travelling
	Failed     // fell back to LAN/IP; the message says why
};

/** What the player asked for when the flow started. */
enum class EDCOnlineIntent : uint8
{
	None,
	Host,
	Join
};

namespace DCOnlineRules
{
	/** key=value lines; ';' and '#' start comments, [sections] are ignored. Keys are case-insensitive. */
	EPIC_AI_MEGA_JAM2_API TMap<FString, FString> ParseKeyValues(const FString& Text);
	/** Fills the credentials; false (with the missing key names, never values) when any is empty. */
	EPIC_AI_MEGA_JAM2_API bool ReadCredentials(const TMap<FString, FString>& Values, FDCOnlineCredentials& Out, FString& OutMissing);
	/** A code of Length characters drawn from Alphabet by the stream. */
	EPIC_AI_MEGA_JAM2_API FString MakeCode(const FString& Alphabet, int32 Length, FRandomStream& Stream);
	/** Untrusted player text to a code: uppercase, spaces and dashes dropped; false when the length or a character is wrong. */
	EPIC_AI_MEGA_JAM2_API bool NormalizeCode(const FString& Input, const FString& Alphabet, int32 Length, FString& OutCode);
	/** True when this state carries the game over the internet transport (net driver swapped). */
	EPIC_AI_MEGA_JAM2_API bool UsesTransport(EDCOnlineState State);
}
