#include "DCOnline.h"

#include "DCGameState.h"
#include "DCOnlineTransport.h"
#include "DCTuning.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogDCOnline, Log, All);

FString UDCOnlineSettings::Message(FName Id, const FString& Detail) const
{
	const FString* Text = Messages.Find(Id);
	return FString::Format(Text ? **Text : *Id.ToString(), {Detail});
}

UDCOnline* UDCOnline::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDCOnline>() : nullptr;
}

void UDCOnline::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UDCOnline::OnPostLoadMap);
	if (GEngine)
	{
		NetFailHandle = GEngine->OnNetworkFailure().AddUObject(this, &UDCOnline::OnNetFailure);
		TravelFailHandle = GEngine->OnTravelFailure().AddUObject(this, &UDCOnline::OnTravelFail);
	}
	// Only evidence runs and running attempts need a tick; the ticker is cheap and stays off the hot path.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDCOnline::Tick), 0.25f);
}

void UDCOnline::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetFailHandle);
		GEngine->OnTravelFailure().Remove(TravelFailHandle);
	}
	UseIp();
	Super::Deinitialize();
}

bool UDCOnline::IsSwitchOn() const
{
	return UDCOnlineSettings::Get().bEnabled && !IsRunningCommandlet() && !FParse::Param(FCommandLine::Get(), TEXT("dceosoff"));
}

FString UDCOnline::MenuKey() const
{
	return FString::Printf(TEXT("%d|%s|%s"), static_cast<int32>(State), *Code, *Status);
}

void UDCOnline::Host()
{
	Start(EDCOnlineIntent::Host);
}

void UDCOnline::Join(const FString& Input)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	if (!DCOnlineRules::NormalizeCode(Input, S.CodeAlphabet, S.CodeLength, Code))
	{
		Status = S.Message(TEXT("BadCode"), FString::FromInt(S.CodeLength));
		UE_LOG(LogDCOnline, Display, TEXT("DCEOS join refused: not a code"));
		return;
	}
	Start(EDCOnlineIntent::Join);
}

IDCOnlineTransport* UDCOnline::FindTransport(FString& OutWhy)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	// The transport module is never loaded by the engine (LoadingPhase None); only an online attempt loads it.
	if (!FModuleManager::Get().ModuleExists(*S.TransportModule) || !FModuleManager::Get().LoadModule(*S.TransportModule))
	{
		OutWhy = FString::Printf(TEXT("module %s missing"), *S.TransportModule);
		return nullptr;
	}
	for (IDCOnlineTransport* T : IModularFeatures::Get().GetModularFeatureImplementations<IDCOnlineTransport>(IDCOnlineTransport::FeatureName()))
	{
		if (T && T->GetId() == S.Transport)
		{
			return T;
		}
	}
	OutWhy = FString::Printf(TEXT("transport %s not registered"), *S.Transport.ToString());
	return nullptr;
}

void UDCOnline::Start(EDCOnlineIntent InIntent)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	if (State == EDCOnlineState::LoggingIn || State == EDCOnlineState::Joining)
	{
		return; // one attempt at a time; the button press is ignored while one runs
	}
	Intent = InIntent;
	++Attempt;
	if (!IsSwitchOn())
	{
		Fail(TEXT("Off"), FString());
		return;
	}
	// Credentials: the local file (or -dceoscreds=<path> for the fallback proof). Values never reach a log.
	FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), S.CredentialsFile);
	FParse::Value(FCommandLine::Get(), TEXT("dceoscreds="), Path);
	FString Text;
	FDCOnlineCredentials Credentials;
	FString Missing;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Fail(TEXT("NoCredentials"), S.CredentialsFile);
		return;
	}
	if (!DCOnlineRules::ReadCredentials(DCOnlineRules::ParseKeyValues(Text), Credentials, Missing))
	{
		Fail(TEXT("NoCredentials"), Missing);
		return;
	}
	FString Why;
	IDCOnlineTransport* Transport = FindTransport(Why);
	if (!Transport)
	{
		Fail(TEXT("LoginFailed"), Why);
		return;
	}
	if (Intent == EDCOnlineIntent::Host)
	{
		FRandomStream Stream(static_cast<int32>(FPlatformTime::Cycles() ^ (FPlatformTime::Cycles64() >> 20)));
		Code = DCOnlineRules::MakeCode(S.CodeAlphabet, S.CodeLength, Stream);
	}
	State = EDCOnlineState::LoggingIn;
	Status = S.Message(TEXT("LoggingIn"));
	Deadline = FPlatformTime::Seconds() + S.TimeoutSeconds;
	UE_LOG(LogDCOnline, Display, TEXT("DCEOS start intent=%s transport=%s"), Intent == EDCOnlineIntent::Host ? TEXT("host") : TEXT("join"), *S.Transport.ToString());
	if (Transport->IsLoggedIn())
	{
		OnLoggedIn();
		return;
	}
	FDCOnlineLoginOptions Options;
	Options.DeviceModel = S.DeviceModel;
	Options.DisplayName = S.DisplayName;
	Options.BucketId = S.BucketId;
	Options.CacheDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EOSCache"));
	IFileManager::Get().MakeDirectory(*Options.CacheDirectory, true);
	Options.bFreshDevice = FParse::Param(FCommandLine::Get(), TEXT("dceosfreshdevice"));
	const int32 This = Attempt;
	TWeakObjectPtr<UDCOnline> Weak(this);
	Transport->Login(Credentials, Options, [Weak, This](bool bOk, const FString& Message)
	{
		UDCOnline* Self = Weak.Get();
		if (!Self || Self->Attempt != This || Self->State != EDCOnlineState::LoggingIn)
		{
			return;
		}
		if (!bOk)
		{
			Self->Fail(TEXT("LoginFailed"), Message);
			return;
		}
		Self->OnLoggedIn();
	});
}

void UDCOnline::OnLoggedIn()
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	FString Why;
	IDCOnlineTransport* Transport = FindTransport(Why);
	if (!Transport)
	{
		Fail(TEXT("LoginFailed"), Why);
		return;
	}
	UE_LOG(LogDCOnline, Display, TEXT("DCEOS login ok user=%s"), *Transport->LocalUserTag());
	if (Intent == EDCOnlineIntent::Host)
	{
		if (!SetTransportDriver(true))
		{
			Fail(TEXT("ListenFailed"), TEXT("net driver"));
			return;
		}
		State = EDCOnlineState::Hosting;
		bAwaitListen = true;
		Status = S.Message(TEXT("Opening"));
		Deadline = FPlatformTime::Seconds() + S.TimeoutSeconds;
		OpenListen();
		return;
	}
	State = EDCOnlineState::Joining;
	Status = S.Message(TEXT("Searching"), Code);
	const int32 This = Attempt;
	TWeakObjectPtr<UDCOnline> Weak(this);
	Transport->Find(Code, UDCTuning::Get().Run.Port, [Weak, This](bool bOk, const FString& Url, const FString& Message)
	{
		UDCOnline* Self = Weak.Get();
		if (!Self || Self->Attempt != This || Self->State != EDCOnlineState::Joining)
		{
			return;
		}
		if (!bOk)
		{
			Self->Fail(TEXT("NotFound"), Self->Code);
			UE_LOG(LogDCOnline, Display, TEXT("DCEOS join lookup failed: %s"), *Message);
			return;
		}
		APlayerController* PC = Self->GetGameInstance() ? Self->GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
		if (!PC || !Self->SetTransportDriver(true))
		{
			Self->Fail(TEXT("ConnectFailed"), TEXT("no player"));
			return;
		}
		Self->Deadline = 0.0; // travelling: the engine reports a failure itself
		Self->Status = UDCOnlineSettings::Get().Message(TEXT("Connecting"), Self->Code);
		UE_LOG(LogDCOnline, Display, TEXT("DCEOS join code=%s found host, travelling over the transport"), *Self->Code);
		PC->ClientTravel(Url, TRAVEL_Absolute);
	});
}

void UDCOnline::OpenListen()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World)
	{
		UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World)), true, TEXT("listen"));
	}
}

void UDCOnline::OnPostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || !bAwaitListen)
	{
		return;
	}
	bAwaitListen = false;
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	UNetDriver* Driver = World->GetNetDriver();
	FString Why;
	IDCOnlineTransport* Transport = FindTransport(Why);
	if (World->GetNetMode() != NM_ListenServer || !Driver || !Transport)
	{
		Fail(TEXT("ListenFailed"), Why);
		return;
	}
	Status = S.Message(TEXT("Advertising"));
	const int32 This = Attempt;
	TWeakObjectPtr<UDCOnline> Weak(this);
	const FString DriverClass = Driver->GetClass()->GetName();
	Transport->Advertise(Code, UDCTuning::Get().Run.MaxPlayers, [Weak, This, DriverClass](bool bOk, const FString& Message)
	{
		UDCOnline* Self = Weak.Get();
		if (!Self || Self->Attempt != This || Self->State != EDCOnlineState::Hosting)
		{
			return;
		}
		if (!bOk)
		{
			Self->Fail(TEXT("AdvertiseFailed"), Message);
			return;
		}
		Self->Deadline = 0.0;
		Self->Status = UDCOnlineSettings::Get().Message(TEXT("Hosting"), Self->Code);
		FString Transport;
		IDCOnlineTransport* T = Self->FindTransport(Transport);
		UE_LOG(LogDCOnline, Display, TEXT("DCEOS host login=ok user=%s code=%s driver=%s"), T ? *T->LocalUserTag() : TEXT("?"), *Self->Code, *DriverClass);
		FString CodeOut;
		if (FParse::Value(FCommandLine::Get(), TEXT("dceoscodeout="), CodeOut))
		{
			FFileHelper::SaveStringToFile(Self->Code, *CodeOut);
		}
	});
}

void UDCOnline::Fail(FName MessageId, const FString& Detail)
{
	const UDCOnlineSettings& S = UDCOnlineSettings::Get();
	const EDCOnlineState Was = State;
	Status = S.Message(MessageId, Detail);
	UE_LOG(LogDCOnline, Display, TEXT("DCEOS fallback reason=%s intent=%s: %s"), *MessageId.ToString(), Intent == EDCOnlineIntent::Host ? TEXT("host") : TEXT("join"), *Status);
	State = EDCOnlineState::Failed;
	Deadline = 0.0;
	bAwaitListen = false;
	FString Unused;
	if (IDCOnlineTransport* T = (Was == EDCOnlineState::Hosting ? FindTransport(Unused) : nullptr))
	{
		T->StopAdvertising();
	}
	SetTransportDriver(false);
	if (Intent == EDCOnlineIntent::Host)
	{
		// The fallback host: the same listen map over IP, friends on the network (or by IP) still join.
		OpenListen();
	}
}

void UDCOnline::UseIp()
{
	if (State == EDCOnlineState::Hosting)
	{
		FString Unused;
		if (IDCOnlineTransport* T = FindTransport(Unused))
		{
			T->StopAdvertising();
		}
	}
	SetTransportDriver(false);
	if (State != EDCOnlineState::Failed)
	{
		Status.Reset(); // keep a fallback message on screen; it explains why the LAN path is running
	}
	State = EDCOnlineState::Off;
	Intent = EDCOnlineIntent::None;
	++Attempt;
}

bool UDCOnline::SetTransportDriver(bool bOn)
{
	if (!GEngine || bOn == bDriverSwapped)
	{
		return true;
	}
	FString Path;
	if (bOn)
	{
		FString Why;
		IDCOnlineTransport* T = FindTransport(Why);
		Path = T ? T->NetDriverClassPath() : FString();
		if (Path.IsEmpty() || !LoadClass<UNetDriver>(nullptr, *Path))
		{
			return false;
		}
	}
	for (FNetDriverDefinition& Def : GEngine->NetDriverDefinitions)
	{
		if (Def.DefName != NAME_GameNetDriver)
		{
			continue;
		}
		if (bOn)
		{
			SavedDriver = Def.DriverClassName;
			SavedFallback = Def.DriverClassNameFallback;
			Def.DriverClassName = FName(*Path);
			Def.DriverClassNameFallback = SavedDriver;
		}
		else
		{
			Def.DriverClassName = SavedDriver;
			Def.DriverClassNameFallback = SavedFallback;
		}
		bDriverSwapped = bOn;
		UE_LOG(LogDCOnline, Display, TEXT("DCEOS GameNetDriver -> %s"), *Def.DriverClassName.ToString());
		return true;
	}
	return false;
}

void UDCOnline::OnNetFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	if (State == EDCOnlineState::Joining)
	{
		Fail(TEXT("ConnectFailed"), ENetworkFailure::ToString(Type));
	}
}

void UDCOnline::OnTravelFail(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (State == EDCOnlineState::Joining)
	{
		Fail(TEXT("ConnectFailed"), ETravelFailure::ToString(Type));
	}
}

bool UDCOnline::Tick(float)
{
	const double Now = FPlatformTime::Seconds();
	if (Deadline > 0.0 && Now > Deadline && (State == EDCOnlineState::LoggingIn || State == EDCOnlineState::Joining || State == EDCOnlineState::Hosting))
	{
		Fail(TEXT("Timeout"), FString());
	}
	// Evidence frames (-dceosshot=<file>): the lobby 3 s after the code (or a fallback message) went up.
	const FString Key = MenuKey();
	if (Key != ShotKey)
	{
		ShotKey = Key;
		ShotKeyTime = Now;
	}
	FString ShotFile;
	if (!bShotTaken && Now - ShotKeyTime > 3.0 && ((State == EDCOnlineState::Hosting && Deadline == 0.0) || State == EDCOnlineState::Failed)
		&& GetGameInstance() && GetGameInstance()->GetWorld() && GetGameInstance()->GetWorld()->GetNetMode() == NM_ListenServer
		&& FParse::Value(FCommandLine::Get(), TEXT("dceosshot="), ShotFile))
	{
		bShotTaken = true;
		FScreenshotRequest::RequestScreenshot(ShotFile, true, false);
		UE_LOG(LogDCOnline, Display, TEXT("DCEOS shot %s state=%d"), *FPaths::GetCleanFilename(ShotFile), static_cast<int32>(State));
	}
	// Evidence runs press the menu buttons on the title screen after 4 real seconds, the same path as a click:
	// -dceoshost (HOST ONLINE), -dceosjoin=<code or @file> (JOIN BY CODE; @file waits for the host's -dceoscodeout).
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
	static bool bPressed = false;
	if (bPressed || !World || World->GetNetMode() != NM_Standalone || !GS || GS->Phase != EDCRunPhase::Lobby || World->GetRealTimeSeconds() < 4.0)
	{
		return true;
	}
	FString JoinArg;
	if (FParse::Param(FCommandLine::Get(), TEXT("dceoshost")))
	{
		bPressed = true;
		Host();
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("dceosjoin="), JoinArg))
	{
		FString Input = JoinArg;
		if (JoinArg.StartsWith(TEXT("@")) && !FFileHelper::LoadFileToString(Input, *JoinArg.Mid(1)))
		{
			return true; // the host has not written its code yet
		}
		bPressed = true;
		Join(Input);
	}
	return true;
}
