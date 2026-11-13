#include "DCLanBeacon.h"

#include "DCTuning.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	const TCHAR* PacketTag = TEXT("DCHOST1");
	constexpr double HostTimeoutSeconds = 4.0;
	constexpr int32 MaxPacketBytes = 256;
	constexpr int32 MaxNameChars = 32;
	constexpr int32 MaxHosts = 16;
}

FString UDCLanBeacon::MakePacket(int32 GamePort, const FString& Name, int32 Players)
{
	return FString::Printf(TEXT("%s|%d|%d|%s"), PacketTag, GamePort, Players, *Name.Left(MaxNameChars).Replace(TEXT("|"), TEXT(" ")));
}

bool UDCLanBeacon::ParsePacket(const FString& Text, int32& OutPort, FString& OutName, int32& OutPlayers)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("|"), false);
	if (Parts.Num() != 4 || Parts[0] != PacketTag || !Parts[1].IsNumeric() || !Parts[2].IsNumeric())
	{
		return false;
	}
	OutPort = FCString::Atoi(*Parts[1]);
	OutPlayers = FCString::Atoi(*Parts[2]);
	OutName = Parts[3].Left(MaxNameChars);
	return OutPort > 0 && OutPort < 65536 && OutPlayers >= 0 && OutPlayers <= 16;
}

void UDCLanBeacon::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UDCLanBeacon::StartAnnouncing(int32 GamePort, const FString& Name)
{
	Stop();
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	Socket = Sockets ? Sockets->CreateSocket(NAME_DGram, TEXT("DCLanBeaconSend"), false) : nullptr;
	if (!Socket)
	{
		return;
	}
	Socket->SetBroadcast(true);
	Socket->SetNonBlocking(true);
	bAnnouncing = true;
	AnnouncedPort = GamePort;
	AnnouncedName = Name;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDCLanBeacon::Tick), 0.25f);
}

void UDCLanBeacon::StartListening()
{
	Stop();
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	Socket = Sockets ? Sockets->CreateSocket(NAME_DGram, TEXT("DCLanBeaconListen"), false) : nullptr;
	if (!Socket)
	{
		return;
	}
	Socket->SetReuseAddr(true);
	Socket->SetNonBlocking(true);
	TSharedRef<FInternetAddr> Any = Sockets->CreateInternetAddr();
	Any->SetAnyAddress();
	Any->SetPort(UDCTuning::Get().Run.LanBeaconPort);
	if (!Socket->Bind(*Any))
	{
		UE_LOG(LogTemp, Warning, TEXT("DCLanBeacon: cannot bind port %d; LAN list disabled (IP join still works)"), UDCTuning::Get().Run.LanBeaconPort);
	}
	bAnnouncing = false;
	bListening = true;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDCLanBeacon::Tick), 0.25f);
}

void UDCLanBeacon::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	bAnnouncing = false;
	bListening = false;
}

bool UDCLanBeacon::Tick(float DeltaSeconds)
{
	(bAnnouncing ? Send() : Receive());
	return true;
}

void UDCLanBeacon::Send()
{
	const double Now = FPlatformTime::Seconds();
	if (!Socket || Now < NextSend)
	{
		return;
	}
	NextSend = Now + 1.0;
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Broadcast = Sockets->CreateInternetAddr();
	Broadcast->SetBroadcastAddress();
	Broadcast->SetPort(UDCTuning::Get().Run.LanBeaconPort);
	const FTCHARToUTF8 Utf8(*MakePacket(AnnouncedPort, AnnouncedName, AnnouncedPlayers));
	int32 Sent = 0;
	Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *Broadcast);
}

void UDCLanBeacon::Receive()
{
	if (!Socket)
	{
		return;
	}
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> From = Sockets->CreateInternetAddr();
	uint8 Buffer[MaxPacketBytes + 1];
	int32 Read = 0;
	uint32 Pending = 0;
	const double Now = FPlatformTime::Seconds();
	for (auto It = Heard.CreateIterator(); It; ++It)
	{
		if (Now - It.Value().LastHeard >= HostTimeoutSeconds)
		{
			It.RemoveCurrent(); // gone quiet: forget it
		}
	}
	for (int32 Guard = 0; Guard < 16 && Socket->HasPendingData(Pending); ++Guard)
	{
		if (!Socket->RecvFrom(Buffer, MaxPacketBytes, Read, *From) || Read <= 0)
		{
			break;
		}
		Buffer[Read] = 0;
		const FString Text = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer));
		int32 Port = 0, Players = 0;
		FString Name;
		if (!ParsePacket(Text, Port, Name, Players))
		{
			continue; // outside input is data: anything malformed is ignored
		}
		// One entry per game (address and port): two hosts on one machine both show.
		const FString Address = FString::Printf(TEXT("%s:%d"), *From->ToString(false), Port);
		if (!Heard.Contains(Address) && Heard.Num() >= MaxHosts)
		{
			continue; // a bounded list: a flood of announcements cannot grow it
		}
		FDCLanHost& Host = Heard.FindOrAdd(Address);
		Host.Address = Address;
		Host.Name = Name;
		Host.Players = Players;
		Host.LastHeard = FPlatformTime::Seconds();
	}
}

TArray<FDCLanHost> UDCLanBeacon::GetHosts() const
{
	TArray<FDCLanHost> Out;
	const double Now = FPlatformTime::Seconds();
	for (const TPair<FString, FDCLanHost>& Pair : Heard)
	{
		if (Now - Pair.Value.LastHeard < HostTimeoutSeconds)
		{
			Out.Add(Pair.Value);
		}
	}
	return Out;
}
