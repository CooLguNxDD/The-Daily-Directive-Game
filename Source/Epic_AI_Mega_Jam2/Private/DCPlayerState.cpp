#include "DCPlayerState.h"

#include "DCTuning.h"
#include "Net/UnrealNetwork.h"

ADCPlayerState::ADCPlayerState()
{
	SetNetUpdateFrequency(20.f);
	bUseCustomPlayerNames = true; // GetPlayerName() returns the call sign, never the network name (R8)
}

FString ADCPlayerState::GetPlayerNameCustom() const
{
	return CallSign.IsEmpty() ? FString(TEXT("CORRESPONDENT")) : CallSign;
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
	DOREPLIFETIME(ADCPlayerState, CallSign);
	DOREPLIFETIME(ADCPlayerState, OutfitCode);
	DOREPLIFETIME(ADCPlayerState, CardChargeIds);
	DOREPLIFETIME(ADCPlayerState, CardChargeLeft);
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

void ADCPlayerState::SyncCardCharges()
{
	CardChargeIds.Reset();
	CardChargeLeft.Reset();
	for (const TPair<FName, int32>& Pair : HostCardCharges)
	{
		CardChargeIds.Add(Pair.Key);
		CardChargeLeft.Add(Pair.Value);
	}
	ForceNetUpdate();
}

int32 ADCPlayerState::CardCharges(FName CardId) const
{
	const int32 Index = CardChargeIds.IndexOfByKey(CardId);
	return CardChargeLeft.IsValidIndex(Index) ? CardChargeLeft[Index] : 0;
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
