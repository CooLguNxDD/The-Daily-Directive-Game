// LAN discovery (R12): a listen host broadcasts a small UDP announcement on the local network; the title menu
// listens and lists the hosts it hears, each with a JOIN button. The packet is data, parsed and checked, never trusted.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DCLanBeacon.generated.h"

class FSocket;

USTRUCT()
struct FDCLanHost
{
	GENERATED_BODY()
	UPROPERTY() FString Address;
	UPROPERTY() FString Name;
	UPROPERTY() int32 Players = 0;
	double LastHeard = 0.0;
};

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCLanBeacon : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** Host: announce this game (port, name, player count) every second until stopped. */
	void StartAnnouncing(int32 GamePort, const FString& Name);
	void SetPlayerCount(int32 Count) { AnnouncedPlayers = Count; }
	/** Title menu: listen for announcements. */
	void StartListening();
	bool IsListening() const { return bListening; }
	void Stop();

	/** Hosts heard in the last few seconds. */
	TArray<FDCLanHost> GetHosts() const;

	/** Pure: the packet text and its parser (tested). */
	static FString MakePacket(int32 GamePort, const FString& Name, int32 Players);
	static bool ParsePacket(const FString& Text, int32& OutPort, FString& OutName, int32& OutPlayers);

private:
	bool Tick(float DeltaSeconds);
	void Send();
	void Receive();

	FSocket* Socket = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	bool bAnnouncing = false;
	bool bListening = false;
	int32 AnnouncedPort = 0;
	FString AnnouncedName;
	int32 AnnouncedPlayers = 1;
	double NextSend = 0.0;
	TMap<FString, FDCLanHost> Heard;
};
