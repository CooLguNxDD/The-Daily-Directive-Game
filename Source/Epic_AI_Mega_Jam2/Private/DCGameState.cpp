#include "DCGameState.h"

#include "DCEnvironment.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 MaxBlastRecords = 32;
}

ADCGameState::ADCGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADCGameState::BeginPlay()
{
	Super::BeginPlay();
	// Lights, sky, ground and post-process are local on every machine (never replicated).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Environment = GetWorld()->SpawnActor<ADCEnvironment>(ADCEnvironment::StaticClass(), FTransform::Identity, Params);
}

void ADCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCGameState, Blasts);
	DOREPLIFETIME(ADCGameState, Phase);
	DOREPLIFETIME(ADCGameState, SectorIndex);
	DOREPLIFETIME(ADCGameState, SectorEndTime);
	DOREPLIFETIME(ADCGameState, RunSeed);
}

int32 ADCGameState::AddBlast(const FVector& Location, float Radius, FName StratagemId)
{
	check(HasAuthority());
	FDCBlastRecord& Rec = Blasts.AddDefaulted_GetRef();
	Rec.Id = NextBlastId++;
	Rec.Location = Location;
	Rec.Time = Now();
	Rec.Radius = Radius;
	Rec.StratagemId = StratagemId;
	if (Blasts.Num() > MaxBlastRecords)
	{
		Blasts.RemoveAt(0, Blasts.Num() - MaxBlastRecords);
	}
	return Rec.Id;
}

const FDCBlastRecord* ADCGameState::FindBlast(int32 Id) const
{
	return Blasts.FindByPredicate([Id](const FDCBlastRecord& B) { return B.Id == Id; });
}
