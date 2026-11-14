#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DCPhotoProbe.h"
#include "DCTypes.h"
#include "DCPlayerController.generated.h"

class UDCInputSet;
class UTexture;
class UTextureRenderTarget2D;
class SWidget;

/** A photo as the photographer's machine holds it: the captured image plus its grade. */
USTRUCT()
struct FDCLocalPhoto
{
	GENERATED_BODY()
	UPROPERTY() int32 PhotoId = 0;
	/** The printed photo (newsprint treatment, R5) every screen shows. */
	UPROPERTY() TObjectPtr<UTexture> Image;
	UPROPERTY() FDCPhotoGrade Grade;
	/** The print's pixels (BGRA), uploaded as JPEG when the shot makes the final cut. */
	TArray<FColor> Pixels;
	int32 Width = 0;
	int32 Height = 0;
	UPROPERTY() bool bConfirmed = false;
	FDCShotView View;
	float ShotTime = 0.f;
	double TakenReal = 0.0;
};

/** One photo on its way to another machine, split into RPC-sized chunks. */
struct FDCPhotoSend
{
	int32 PhotoId = 0;
	TArray<uint8> Bytes;
	int32 NextChunk = 0;
};

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADCPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	/** -dcperf: once a real second, logs DCPERF with this machine's frame and GPU times (R13 evidence). */
	void TickPerf();
	/** -dcmenuflow=<dir>: frames the title menu, moves the real cursor onto SOLO RUN, clicks it with Slate mouse events
	 *  (the same path as a player), and frames the travel and the first street, then quits (menu evidence). */
	void TickMenuFlow();
	TArray<float> PerfFrameMs;
	double PerfWindowStart = 0.0;
	int32 PerfMaxFalling = 0;
	virtual void Tick(float DeltaSeconds) override;

	UDCInputSet* GetInputSet();
	float GetSensitivity() const { return Sensitivity; }
	void SetSensitivity(float Value);

	// Local photo album (images of this player's shots).
	void AddLocalPhoto(int32 PhotoId, UTextureRenderTarget2D* Image, const FDCPhotoGrade& Preview, const FDCShotView& View, float ShotTime);
	void ConfirmLocalPhoto(int32 PhotoId, const FDCPhotoGrade& Grade);
	const FDCLocalPhoto* FindLocalPhoto(int32 PhotoId) const;
	const FDCLocalPhoto* GetLastPhoto() const { return Album.Num() ? &Album.Last() : nullptr; }
	const TArray<FDCLocalPhoto>& GetAlbum() const { return Album; }

	/** Any player's photo image this machine has (own capture or one received from the host); null while in transit. */
	UTexture* GetPhotoImage(int32 PhotoId) const;

	// Run intent (HUD buttons and the menu call these on the local controller).
	void RequestStartRun() { ServerStartRun(); }
	void RequestRestart() { ServerRestartRun(); }
	void RequestPickUpgrade(FName Id) { ServerPickUpgrade(Id); Confirm(); }
	void ToggleFinalCutPick(int32 PhotoId);
	void RequestSubmitFinalCut() { ServerSubmitFinalCut(LocalPicks); Confirm(); }
	void RequestVote(int32 PhotoId) { ServerVote(PhotoId); Confirm(); }
	const TArray<int32>& GetLocalPicks() const { return LocalPicks; }
	class ADCReelDirector* GetReelDirector() const { return Director; }
	/** R16: send a second take to the host for grading (the image stays here until the reel uploads it). */
	void RequestSecondTake(int32 PhotoId, const FDCShotView& View, int32 ClipIndex) { ServerSecondTake(PhotoId, View.Location, View.Rotation, View.FOV, ClipIndex); }

	// Menu (local).
	void MenuSolo();
	void MenuHost();
	void MenuJoin(const FString& Address);
	void MenuQuit();

	/** Host to the owning client: send these photo images so every machine can show them in the reel and vote. */
	UFUNCTION(Client, Reliable) void ClientUploadPhotos(const TArray<int32>& PhotoIds);

private:
	void OnSensUp();
	/** UI confirm cue for a committed choice (draft, send to press, vote). */
	void Confirm();
	void OnSensDown();
	void UpdateMenu();
	void UpdateCursor();
	void UpdateReel();
	/** Evidence runs: `-dcshotevery=<seconds> -dcshots=<dir>` saves a frame on this machine every N seconds. */
	void UpdateEvidenceShots();
	void UpdateAutoVote();
	bool bAutoVoted = false;
	void PumpSends();
	void HostReceivedPhoto(int32 PhotoId, const TArray<uint8>& Bytes);
	void StoreImage(int32 PhotoId, const TArray<uint8>& Bytes);
	void Reassemble(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes, bool bOnHost);

	UFUNCTION(Server, Reliable) void ServerStartRun();
	UFUNCTION(Server, Reliable) void ServerRestartRun();
	UFUNCTION(Server, Reliable) void ServerPickUpgrade(FName Id);
	UFUNCTION(Server, Reliable) void ServerSubmitFinalCut(const TArray<int32>& PhotoIds);
	UFUNCTION(Server, Reliable) void ServerVote(int32 PhotoId);
	UFUNCTION(Server, Reliable) void ServerSecondTake(int32 PhotoId, FVector_NetQuantize10 CamLoc, FRotator CamRot, float FOV, int32 ClipIndex);
	UFUNCTION(Client, Reliable) void ClientSecondTakeGraded(int32 PhotoId, FDCPhotoGrade Grade);
	UFUNCTION(Client, Reliable) void ClientSecondTakeRejected(int32 PhotoId, int32 ClipIndex);
	/** Clips this player already took a second take of (host side). */
	TSet<int32> SecondTakeClips;
	UFUNCTION(Server, Reliable) void ServerPhotoChunk(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable) void ClientPhotoChunk(int32 PhotoId, int32 Index, int32 Count, const TArray<uint8>& Bytes);

	UPROPERTY() TObjectPtr<UDCInputSet> InputSet;
	UPROPERTY() TArray<FDCLocalPhoto> Album;
	/** Images of other players' photos, decoded from the JPEG bytes the host relayed. */
	UPROPERTY() TMap<int32, TObjectPtr<UTexture>> Library;
	TMap<int32, TArray<TArray<uint8>>> Incoming;
	TArray<FDCPhotoSend> Outgoing;
	TArray<int32> LocalPicks;
	TSharedPtr<SWidget> MenuWidget;
	UPROPERTY() TObjectPtr<class ADCReelDirector> Director;
	uint8 MenuMode = 255;
	/** The LAN hosts the current menu shows ("" = none), to rebuild the menu when the list changes. */
	FString MenuHostsKey;
	bool bLanJoinSent = false;
	float Sensitivity = 1.f;
	double NextEvidenceShotReal = 0.0;
	int32 EvidenceShotIndex = 0;
};
