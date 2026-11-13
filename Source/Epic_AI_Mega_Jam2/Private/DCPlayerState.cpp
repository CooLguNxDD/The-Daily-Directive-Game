#include "DCPlayerState.h"

#include "DCTuning.h"
#include "Net/UnrealNetwork.h"

ADCPlayerState::ADCPlayerState()
{
	SetNetUpdateFrequency(20.f);
}

void ADCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCPlayerState, Points);
	DOREPLIFETIME(ADCPlayerState, ShotsLeft);
	DOREPLIFETIME(ADCPlayerState, FilmPerSector);
	DOREPLIFETIME(ADCPlayerState, Photos);
	DOREPLIFETIME(ADCPlayerState, OwnedStratagems);
	DOREPLIFETIME(ADCPlayerState, CooldownIds);
	DOREPLIFETIME(ADCPlayerState, CooldownTimes);
	DOREPLIFETIME(ADCPlayerState, Kills);
	DOREPLIFETIME(ADCPlayerState, DraftOffer);
	DOREPLIFETIME(ADCPlayerState, Upgrades);
	DOREPLIFETIME(ADCPlayerState, bDraftDone);
	DOREPLIFETIME(ADCPlayerState, Gear);
	DOREPLIFETIME(ADCPlayerState, PhotoMultiplier);
	DOREPLIFETIME(ADCPlayerState, DangerMultiplier);
	DOREPLIFETIME(ADCPlayerState, ExtraFOV);
	DOREPLIFETIME(ADCPlayerState, FinalCutChoices);
	DOREPLIFETIME(ADCPlayerState, FinalCut);
	DOREPLIFETIME(ADCPlayerState, bFinalCutDone);
	DOREPLIFETIME(ADCPlayerState, VotedFor);
}

void ADCPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	const UDCTuning& T = UDCTuning::Get();
	Points = T.Economy.StartPoints;
	FilmPerSector = T.Camera.ShotsPerSector;
	ShotsLeft = FilmPerSector;
	for (const FDCStratagemDef& Def : T.Stratagems)
	{
		if (Def.bStartsUnlocked)
		{
			OwnedStratagems.AddUnique(Def.Id);
		}
	}
}

void ADCPlayerState::AddPhoto(const FDCPhotoRecord& Record)
{
	Photos.Add(Record);
	AddPoints(FMath::RoundToInt(Record.Grade.Score * UDCTuning::Get().Economy.PointsPerScore));
}

bool ADCPlayerState::TrySpend(int32 Cost)
{
	if (Cost > Points)
	{
		return false;
	}
	Points -= Cost;
	return true;
}

bool ADCPlayerState::ConsumeShot()
{
	if (ShotsLeft <= 0)
	{
		return false;
	}
	--ShotsLeft;
	return true;
}

float ADCPlayerState::CooldownReadyTime(FName Id) const
{
	const int32 Index = CooldownIds.IndexOfByKey(Id);
	return CooldownTimes.IsValidIndex(Index) ? CooldownTimes[Index] : 0.f;
}

void ADCPlayerState::SetCooldown(FName Id, float ReadyTime)
{
	const int32 Index = CooldownIds.IndexOfByKey(Id);
	if (CooldownTimes.IsValidIndex(Index))
	{
		CooldownTimes[Index] = ReadyTime;
		return;
	}
	CooldownIds.Add(Id);
	CooldownTimes.Add(ReadyTime);
}

const FDCPhotoRecord* ADCPlayerState::FindPhoto(int32 PhotoId) const
{
	return Photos.FindByPredicate([PhotoId](const FDCPhotoRecord& P) { return P.PhotoId == PhotoId; });
}
