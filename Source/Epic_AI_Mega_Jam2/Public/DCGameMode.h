// Host-only run director: seeds the run, builds each sector's city, spawns waves, calls strikes, books photo
// scores and moves the run through its phases (sector -> draft -> ... -> reel -> final cut -> vote -> front page).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Rules/DCCityPlan.h"
#include "DCGameMode.generated.h"

class ADCCharacter;
class ADCBuilding;
struct FDCStratagemDef;
struct FDCPhotoRecord;
struct FDCPhotoInputs;

UCLASS()
class EPIC_AI_MEGA_JAM2_API ADCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADCGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	void CallStrike(const FDCStratagemDef& Def, const FVector& Target, AController* Caller);
	void OnPhotoGraded(ADCCharacter* Photographer, const FDCPhotoRecord& Record, const FDCPhotoInputs& Inputs);
	void OnPlayerDowned(ADCCharacter* Player);
	void RequestHitstop();
	bool IsCombatLive() const;

	const FDCCityPlan& GetCityPlan() const { return CityPlan; }

private:
	void StartSector(int32 Index);
	void ClearSector();
	void TickWaves(float Now);
	void TickSector(float Now);
	FTransform SpawnTransformFor(int32 PlayerIndex) const;

	FDCCityPlan CityPlan;
	int32 RequestedSeed = 0;
	double HitstopEndReal = 0.0;
	float SectorStartTime = 0.f;
	TArray<int32> WaveSpawned;
	TArray<float> WaveNextTime;
	FRandomStream SectorRng;
};
