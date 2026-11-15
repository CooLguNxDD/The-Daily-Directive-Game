// R26 internet co-op (EOS P2P), the game side. Behind a switch (UDCOnlineSettings::bEnabled, Config/DefaultGame.ini):
// nothing online starts until the player presses HOST ONLINE or JOIN BY CODE. Credentials come from the gitignored
// Config/Custom/EOS.ini (tools/eos/stage_eos_ini.sh copies the human's local file there and into a package). Missing
// credentials, a failed login, a code nobody hosts or a failed listen fall back to the LAN/IP path with a message on the
// menu. GameNetDriver stays the IP driver unless a login succeeded and the player picked online; solo, LAN and the gate
// never create an online platform. Pure parts: Rules/DCOnlineRules.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Rules/DCOnlineRules.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DCOnline.generated.h"

class APlayerController;
class UNetDriver;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Director's Cut Online"))
class EPIC_AI_MEGA_JAM2_API UDCOnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDCOnlineSettings& Get() { return *GetDefault<UDCOnlineSettings>(); }

	/** The switch: off hides the online buttons and nothing online ever loads. */
	UPROPERTY(EditAnywhere, Config, Category = "Online") bool bEnabled = false;
	/** Transport registry id and the module that registers it (loaded on demand only). */
	UPROPERTY(EditAnywhere, Config, Category = "Online") FName Transport = TEXT("EOS");
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString TransportModule = TEXT("DCOnlineEos");
	/** Local credentials file, relative to the project Config directory (gitignored, never staged into a pak). */
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString CredentialsFile = TEXT("Custom/EOS.ini");
	/** Join codes: characters a player cannot confuse, and the length. */
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString CodeAlphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	UPROPERTY(EditAnywhere, Config, Category = "Online") int32 CodeLength = 6;
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString BucketId = TEXT("DirectorsCut:Coop");
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString DeviceModel = TEXT("DirectorsCut PC");
	UPROPERTY(EditAnywhere, Config, Category = "Online") FString DisplayName = TEXT("Photographer");
	/** A login or lookup that takes longer falls back. */
	UPROPERTY(EditAnywhere, Config, Category = "Online") float TimeoutSeconds = 25.f;
	/** Menu wording by id ({0} = detail). */
	UPROPERTY(EditAnywhere, Config, Category = "Online") TMap<FName, FString> Messages;

	FString Message(FName Id, const FString& Detail = FString()) const;
};

UCLASS()
class EPIC_AI_MEGA_JAM2_API UDCOnline : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDCOnline* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsSwitchOn() const;
	/** HOST ONLINE: log in, open the listen map over the transport, advertise a code. Falls back to a LAN host. */
	void Host();
	/** JOIN BY CODE: log in, look the code up, travel over the transport. Falls back with a message (IP join stays). */
	void Join(const FString& Input);
	/** Solo, LAN host and IP join: the IP driver carries the game again and any advertised session stops. */
	void UseIp();

	EDCOnlineState GetState() const { return State; }
	const FString& GetCode() const { return Code; }
	const FString& GetStatus() const { return Status; }
	/** Changes whenever the menu should rebuild (state, code, status). */
	FString MenuKey() const;
	/** True while the internet transport carries this game (the LAN beacon stays quiet then). */
	bool IsCarrying() const { return DCOnlineRules::UsesTransport(State); }

private:
	void Start(EDCOnlineIntent InIntent);
	void OnLoggedIn();
	void Fail(FName MessageId, const FString& Detail);
	bool SetTransportDriver(bool bOn);
	void OnPostLoadMap(UWorld* World);
	void OnNetFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFail(UWorld* World, ETravelFailure::Type Type, const FString& Error);
	bool Tick(float DeltaSeconds);
	void OpenListen();
	class IDCOnlineTransport* FindTransport(FString& OutWhy);

	EDCOnlineState State = EDCOnlineState::Off;
	EDCOnlineIntent Intent = EDCOnlineIntent::None;
	FString Code;
	FString Status;
	double Deadline = 0.0;
	/** Bumped on every new attempt; late callbacks of an older attempt are dropped. */
	int32 Attempt = 0;
	FName SavedDriver;
	FName SavedFallback;
	bool bDriverSwapped = false;
	/** Host: the listen map is opening over the transport; its load advertises the code. */
	bool bAwaitListen = false;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PostLoadHandle;
	FDelegateHandle NetFailHandle;
	FDelegateHandle TravelFailHandle;
};
