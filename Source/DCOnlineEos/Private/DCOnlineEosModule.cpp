// R26 EOS P2P transport (IDCOnlineTransport, id "EOS"). Device-id login through EOS Connect (no Epic account), one EOS
// session per host carrying the join code and the host's product user id, and the engine's EOS socket subsystem
// (SocketSubsystemEOS, Epic relay on by default for NAT) for NetDriverEOS. Built only from the credentials UDCOnline
// hands over; no value of them is ever logged. Every SDK callback runs on the game thread (EOSShared ticks the platform).
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "DCOnlineTransport.h"
#include "Features/IModularFeatures.h"
#include "IEOSSDKManager.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystemEOS.h"
#include "SocketSubsystemEOSUtils.h"

#include "eos_connect.h"
#include "eos_sdk.h"
#include "eos_sessions.h"

DEFINE_LOG_CATEGORY_STATIC(LogDCOnlineEos, Log, All);

namespace DCOnlineEos
{
	static const char* SessionName = "DCGame";
	static const char* CodeKey = "DCCODE";
	static const char* HostKey = "DCHOST";
	static constexpr int32 FindTries = 6;
	static constexpr float FindRetrySeconds = 2.f;

	FString PuidString(EOS_ProductUserId Id)
	{
		char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
		int32_t Len = sizeof(Buffer);
		return Id && EOS_ProductUserId_ToString(Id, Buffer, &Len) == EOS_EResult::EOS_Success ? FString(UTF8_TO_TCHAR(Buffer)) : FString();
	}

	FString ResultText(EOS_EResult Result)
	{
		return UTF8_TO_TCHAR(EOS_EResult_ToString(Result));
	}
}

/** What the EOS socket subsystem asks of its owner: the logged-in user. */
class FDCEosSocketUtils : public ISocketSubsystemEOSUtils
{
public:
	EOS_ProductUserId User = nullptr;

	virtual EOS_ProductUserId GetLocalUserId() override { return User; }
	virtual FString GetSessionId() override { return FString(); }
	virtual FName GetSubsystemInstanceName() override { return NAME_None; } // the game world (GetWorldForOnline)
	virtual bool IsLoggedIn() override { return User != nullptr; }
};

class FDCOnlineEosTransport : public IDCOnlineTransport
{
public:
	virtual FName GetId() const override { return TEXT("EOS"); }
	virtual bool IsLoggedIn() const override { return User != nullptr && SocketSubsystem.IsValid(); }
	virtual FString LocalUserTag() const override { return DCOnlineEos::PuidString(User).Right(6); }
	virtual FString NetDriverClassPath() const override { return TEXT("/Script/SocketSubsystemEOS.NetDriverEOS"); }

	virtual void Login(const FDCOnlineCredentials& C, const FDCOnlineLoginOptions& Options, TFunction<void(bool, const FString&)> Done) override
	{
		LoginOptions = Options;
		if (!Platform.IsValid())
		{
			IEOSSDKManager* Manager = IEOSSDKManager::Get();
			if (!Manager || !Manager->IsInitialized())
			{
				Done(false, TEXT("EOS SDK not initialized"));
				return;
			}
			const FTCHARToUTF8 ProductId(*C.ProductId), SandboxId(*C.SandboxId), DeploymentId(*C.DeploymentId);
			const FTCHARToUTF8 ClientId(*C.ClientId), ClientSecret(*C.ClientSecret), CacheDir(*Options.CacheDirectory);
			EOS_Platform_Options P = {};
			P.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
			P.ProductId = ProductId.Get();
			P.SandboxId = SandboxId.Get();
			P.DeploymentId = DeploymentId.Get();
			P.ClientCredentials.ClientId = ClientId.Get();
			P.ClientCredentials.ClientSecret = ClientSecret.Get();
			P.bIsServer = EOS_FALSE;
			P.Flags = EOS_PF_DISABLE_OVERLAY;
			P.CacheDirectory = CacheDir.Length() ? CacheDir.Get() : nullptr;
			Platform = Manager->CreatePlatform(P);
			if (!Platform.IsValid())
			{
				Done(false, TEXT("EOS platform not created (check the product ids)"));
				return;
			}
		}
		Pending = MoveTemp(Done);
		if (Options.bFreshDevice)
		{
			EOS_Connect_DeleteDeviceIdOptions Del = {};
			Del.ApiVersion = EOS_CONNECT_DELETEDEVICEID_API_LATEST;
			EOS_Connect_DeleteDeviceId(Connect(), &Del, this, [](const EOS_Connect_DeleteDeviceIdCallbackInfo* Data)
			{
				static_cast<FDCOnlineEosTransport*>(Data->ClientData)->CreateDevice();
			});
			return;
		}
		CreateDevice();
	}

	virtual void Advertise(const FString& Code, int32 MaxPlayers, TFunction<void(bool, const FString&)> Done) override
	{
		const FTCHARToUTF8 Bucket(*LoginOptions.BucketId), CodeUtf8(*Code), Host(*DCOnlineEos::PuidString(User));
		EOS_HSessions Sessions = EOS_Platform_GetSessionsInterface(*Platform);
		EOS_Sessions_CreateSessionModificationOptions Create = {};
		Create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
		Create.SessionName = DCOnlineEos::SessionName;
		Create.BucketId = Bucket.Get();
		Create.MaxPlayers = static_cast<uint32_t>(FMath::Max(MaxPlayers, 2));
		Create.LocalUserId = User;
		EOS_HSessionModification Mod = nullptr;
		EOS_EResult R = EOS_Sessions_CreateSessionModification(Sessions, &Create, &Mod);
		if (R != EOS_EResult::EOS_Success)
		{
			Done(false, DCOnlineEos::ResultText(R));
			return;
		}
		EOS_SessionModification_SetPermissionLevelOptions Perm = {};
		Perm.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
		Perm.PermissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
		EOS_SessionModification_SetPermissionLevel(Mod, &Perm);
		EOS_SessionModification_SetJoinInProgressAllowedOptions Jip = {};
		Jip.ApiVersion = EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST;
		Jip.bAllowJoinInProgress = EOS_TRUE;
		EOS_SessionModification_SetJoinInProgressAllowed(Mod, &Jip);
		const TPair<const char*, const char*> Attributes[] = {{DCOnlineEos::CodeKey, CodeUtf8.Get()}, {DCOnlineEos::HostKey, Host.Get()}};
		for (const TPair<const char*, const char*>& A : Attributes)
		{
			EOS_Sessions_AttributeData Data = {};
			Data.ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
			Data.Key = A.Key;
			Data.Value.AsUtf8 = A.Value;
			Data.ValueType = EOS_EAttributeType::EOS_AT_STRING;
			EOS_SessionModification_AddAttributeOptions Add = {};
			Add.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
			Add.SessionAttribute = &Data;
			Add.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
			EOS_SessionModification_AddAttribute(Mod, &Add);
		}
		EOS_Sessions_UpdateSessionOptions Update = {};
		Update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
		Update.SessionModificationHandle = Mod;
		PendingAdvertise = MoveTemp(Done);
		EOS_Sessions_UpdateSession(Sessions, &Update, this, [](const EOS_Sessions_UpdateSessionCallbackInfo* Data)
		{
			FDCOnlineEosTransport* Self = static_cast<FDCOnlineEosTransport*>(Data->ClientData);
			const bool bOk = Data->ResultCode == EOS_EResult::EOS_Success;
			Self->bAdvertising = bOk;
			if (Self->PendingAdvertise)
			{
				TFunction<void(bool, const FString&)> Cb = MoveTemp(Self->PendingAdvertise);
				Cb(bOk, DCOnlineEos::ResultText(Data->ResultCode));
			}
		});
		EOS_SessionModification_Release(Mod);
	}

	virtual void StopAdvertising() override
	{
		if (!bAdvertising || !Platform.IsValid())
		{
			return;
		}
		bAdvertising = false;
		EOS_Sessions_DestroySessionOptions Destroy = {};
		Destroy.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
		Destroy.SessionName = DCOnlineEos::SessionName;
		EOS_Sessions_DestroySession(EOS_Platform_GetSessionsInterface(*Platform), &Destroy, nullptr, [](const EOS_Sessions_DestroySessionCallbackInfo*) {});
	}

	virtual void Find(const FString& Code, int32 Port, TFunction<void(bool, const FString&, const FString&)> Done) override
	{
		FindCode = Code;
		FindPort = Port;
		FindTry = 0;
		PendingFind = MoveTemp(Done);
		StartFind();
	}

	void Shutdown()
	{
		StopAdvertising();
		if (SocketSubsystem.IsValid())
		{
			SocketSubsystem->Shutdown();
			SocketSubsystem.Reset();
		}
		Platform.Reset();
		User = nullptr;
	}

private:
	EOS_HConnect Connect() const { return EOS_Platform_GetConnectInterface(*Platform); }

	void CreateDevice()
	{
		const FTCHARToUTF8 Model(*LoginOptions.DeviceModel);
		EOS_Connect_CreateDeviceIdOptions Opt = {};
		Opt.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
		Opt.DeviceModel = Model.Get();
		EOS_Connect_CreateDeviceId(Connect(), &Opt, this, [](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
		{
			FDCOnlineEosTransport* Self = static_cast<FDCOnlineEosTransport*>(Data->ClientData);
			// An existing device id is the normal case on every launch after the first.
			if (Data->ResultCode != EOS_EResult::EOS_Success && Data->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
			{
				Self->Finish(false, FString::Printf(TEXT("device id: %s"), *DCOnlineEos::ResultText(Data->ResultCode)));
				return;
			}
			Self->LoginDevice();
		});
	}

	void LoginDevice()
	{
		const FTCHARToUTF8 Name(*LoginOptions.DisplayName);
		EOS_Connect_Credentials Cred = {};
		Cred.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Cred.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
		Cred.Token = nullptr;
		EOS_Connect_UserLoginInfo Info = {};
		Info.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
		Info.DisplayName = Name.Get();
		EOS_Connect_LoginOptions Opt = {};
		Opt.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Opt.Credentials = &Cred;
		Opt.UserLoginInfo = &Info;
		EOS_Connect_Login(Connect(), &Opt, this, [](const EOS_Connect_LoginCallbackInfo* Data)
		{
			FDCOnlineEosTransport* Self = static_cast<FDCOnlineEosTransport*>(Data->ClientData);
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Self->LoggedIn(Data->LocalUserId);
				return;
			}
			if (Data->ResultCode != EOS_EResult::EOS_InvalidUser || !Data->ContinuanceToken)
			{
				Self->Finish(false, FString::Printf(TEXT("connect login: %s"), *DCOnlineEos::ResultText(Data->ResultCode)));
				return;
			}
			// First launch on this device: make the product user.
			EOS_Connect_CreateUserOptions Create = {};
			Create.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
			Create.ContinuanceToken = Data->ContinuanceToken;
			EOS_Connect_CreateUser(Self->Connect(), &Create, Self, [](const EOS_Connect_CreateUserCallbackInfo* User)
			{
				FDCOnlineEosTransport* Me = static_cast<FDCOnlineEosTransport*>(User->ClientData);
				if (User->ResultCode != EOS_EResult::EOS_Success)
				{
					Me->Finish(false, FString::Printf(TEXT("create user: %s"), *DCOnlineEos::ResultText(User->ResultCode)));
					return;
				}
				Me->LoggedIn(User->LocalUserId);
			});
		});
	}

	void LoggedIn(EOS_ProductUserId Id)
	{
		User = Id;
		if (!SocketSubsystem.IsValid())
		{
			Utils = MakeShared<FDCEosSocketUtils, ESPMode::ThreadSafe>();
			SocketSubsystem = MakeShareable(new FSocketSubsystemEOS(Platform, Utils));
			FString Error;
			if (!SocketSubsystem->Init(Error))
			{
				SocketSubsystem.Reset();
				User = nullptr;
				Finish(false, FString::Printf(TEXT("socket subsystem: %s"), *Error));
				return;
			}
		}
		Utils->User = Id;
		Finish(true, FString());
	}

	void Finish(bool bOk, const FString& Message)
	{
		UE_LOG(LogDCOnlineEos, Display, TEXT("DCEOS login %s %s"), bOk ? TEXT("ok") : TEXT("failed"), *Message);
		if (Pending)
		{
			TFunction<void(bool, const FString&)> Cb = MoveTemp(Pending);
			Cb(bOk, Message);
		}
	}

	void StartFind()
	{
		++FindTry;
		const FTCHARToUTF8 CodeUtf8(*FindCode), Bucket(*LoginOptions.BucketId);
		EOS_HSessions Sessions = EOS_Platform_GetSessionsInterface(*Platform);
		if (Search)
		{
			EOS_SessionSearch_Release(Search);
			Search = nullptr;
		}
		EOS_Sessions_CreateSessionSearchOptions Make = {};
		Make.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
		Make.MaxSearchResults = 4;
		if (EOS_Sessions_CreateSessionSearch(Sessions, &Make, &Search) != EOS_EResult::EOS_Success)
		{
			EndFind(false, FString(), TEXT("search not created"));
			return;
		}
		const TPair<const char*, const char*> Params[] = {{DCOnlineEos::CodeKey, CodeUtf8.Get()}, {EOS_SESSIONS_SEARCH_BUCKET_ID, Bucket.Get()}};
		for (const TPair<const char*, const char*>& Param : Params)
		{
			EOS_Sessions_AttributeData Data = {};
			Data.ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
			Data.Key = Param.Key;
			Data.Value.AsUtf8 = Param.Value;
			Data.ValueType = EOS_EAttributeType::EOS_AT_STRING;
			EOS_SessionSearch_SetParameterOptions Set = {};
			Set.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
			Set.Parameter = &Data;
			Set.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
			EOS_SessionSearch_SetParameter(Search, &Set);
		}
		EOS_SessionSearch_FindOptions FindOpt = {};
		FindOpt.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
		FindOpt.LocalUserId = User;
		EOS_SessionSearch_Find(Search, &FindOpt, this, [](const EOS_SessionSearch_FindCallbackInfo* Data)
		{
			static_cast<FDCOnlineEosTransport*>(Data->ClientData)->OnFound(Data->ResultCode);
		});
	}

	void OnFound(EOS_EResult Result)
	{
		FString Host;
		if (Result == EOS_EResult::EOS_Success && Search)
		{
			EOS_SessionSearch_GetSearchResultCountOptions CountOpt = {};
			CountOpt.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
			const uint32_t Count = EOS_SessionSearch_GetSearchResultCount(Search, &CountOpt);
			for (uint32_t i = 0; i < Count && Host.IsEmpty(); ++i)
			{
				EOS_SessionSearch_CopySearchResultByIndexOptions CopyOpt = {};
				CopyOpt.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
				CopyOpt.SessionIndex = i;
				EOS_HSessionDetails Details = nullptr;
				if (EOS_SessionSearch_CopySearchResultByIndex(Search, &CopyOpt, &Details) != EOS_EResult::EOS_Success)
				{
					continue;
				}
				EOS_SessionDetails_CopySessionAttributeByKeyOptions AttrOpt = {};
				AttrOpt.ApiVersion = EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYKEY_API_LATEST;
				AttrOpt.AttrKey = DCOnlineEos::HostKey;
				EOS_SessionDetails_Attribute* Attr = nullptr;
				if (EOS_SessionDetails_CopySessionAttributeByKey(Details, &AttrOpt, &Attr) == EOS_EResult::EOS_Success && Attr && Attr->Data && Attr->Data->ValueType == EOS_EAttributeType::EOS_AT_STRING)
				{
					Host = UTF8_TO_TCHAR(Attr->Data->Value.AsUtf8);
				}
				EOS_SessionDetails_Attribute_Release(Attr);
				EOS_SessionDetails_Release(Details);
			}
		}
		// The host id is outside input: it must parse as a product user id before it becomes a travel url.
		const bool bValid = !Host.IsEmpty() && EOS_ProductUserId_IsValid(EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Host))) == EOS_TRUE;
		if (bValid)
		{
			UE_LOG(LogDCOnlineEos, Display, TEXT("DCEOS found host=%s try=%d"), *Host.Right(6), FindTry);
			EndFind(true, FString::Printf(TEXT("[%s%s%s]:%d"), EOS_CONNECTION_URL_PREFIX, EOS_URL_SEPARATOR, *Host, FindPort), FString());
			return;
		}
		if (FindTry >= DCOnlineEos::FindTries)
		{
			EndFind(false, FString(), FString::Printf(TEXT("no session (%s)"), *DCOnlineEos::ResultText(Result)));
			return;
		}
		// A new session takes a moment to index: look again shortly.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			if (PendingFind)
			{
				StartFind();
			}
			return false;
		}), DCOnlineEos::FindRetrySeconds);
	}

	void EndFind(bool bOk, const FString& Url, const FString& Message)
	{
		if (Search)
		{
			EOS_SessionSearch_Release(Search);
			Search = nullptr;
		}
		if (PendingFind)
		{
			TFunction<void(bool, const FString&, const FString&)> Cb = MoveTemp(PendingFind);
			Cb(bOk, Url, Message);
		}
	}

	IEOSPlatformHandlePtr Platform;
	TSharedPtr<FDCEosSocketUtils, ESPMode::ThreadSafe> Utils;
	TSharedPtr<FSocketSubsystemEOS, ESPMode::ThreadSafe> SocketSubsystem;
	EOS_ProductUserId User = nullptr;
	FDCOnlineLoginOptions LoginOptions;
	TFunction<void(bool, const FString&)> Pending;
	TFunction<void(bool, const FString&)> PendingAdvertise;
	TFunction<void(bool, const FString&, const FString&)> PendingFind;
	EOS_HSessionSearch Search = nullptr;
	FString FindCode;
	int32 FindPort = 0;
	int32 FindTry = 0;
	bool bAdvertising = false;
};

class FDCOnlineEosModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(IDCOnlineTransport::FeatureName(), &Transport);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(IDCOnlineTransport::FeatureName(), &Transport);
		Transport.Shutdown();
	}

private:
	FDCOnlineEosTransport Transport;
};

IMPLEMENT_MODULE(FDCOnlineEosModule, DCOnlineEos);
