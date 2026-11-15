// R26 internet co-op: the transport seam. The game module never links an online SDK. A transport module (DCOnlineEos,
// LoadingPhase None, loaded on demand only) registers itself as a modular feature under FeatureName(); UDCOnline
// (DCOnline.cpp) loads the module named in data and finds the transport by its id. Only the transport changes: the
// host-authoritative game, replication and voxel ops run over whatever net driver the transport names.
#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

/** Product credentials read from the local, gitignored file (never logged, never saved). */
struct FDCOnlineCredentials
{
	FString ProductId;
	FString SandboxId;
	FString DeploymentId;
	FString ClientId;
	FString ClientSecret;
};

/** Login and session knobs (from UDCOnlineSettings rows). */
struct FDCOnlineLoginOptions
{
	FString DeviceModel;
	FString DisplayName;
	FString BucketId;
	FString CacheDirectory;
	/** Test runs only (-dceosfreshdevice): drop the stored device id first, so a second instance on one PC is a new user. */
	bool bFreshDevice = false;
};

/** One internet transport (EOS today). Every callback runs on the game thread; messages never carry a secret. */
class IDCOnlineTransport : public IModularFeature
{
public:
	static FName FeatureName() { return TEXT("DCOnlineTransport"); }

	virtual ~IDCOnlineTransport() = default;
	/** Registry key (matches UDCOnlineSettings::Transport). */
	virtual FName GetId() const = 0;
	/** Start the platform and log in anonymously. Done(bOk, Message). */
	virtual void Login(const FDCOnlineCredentials& Credentials, const FDCOnlineLoginOptions& Options, TFunction<void(bool, const FString&)> Done) = 0;
	virtual bool IsLoggedIn() const = 0;
	/** A short, non-secret tag of the local user for logs. */
	virtual FString LocalUserTag() const = 0;
	/** Host: publish a session other players find by this code. Done(bOk, Message). */
	virtual void Advertise(const FString& Code, int32 MaxPlayers, TFunction<void(bool, const FString&)> Done) = 0;
	virtual void StopAdvertising() = 0;
	/** Client: look the code up. Done(bOk, ConnectUrl, Message); the url goes straight to ClientTravel. */
	virtual void Find(const FString& Code, int32 Port, TFunction<void(bool, const FString&, const FString&)> Done) = 0;
	/** The net driver class for GameNetDriver while this transport carries the game. */
	virtual FString NetDriverClassPath() const = 0;
};
