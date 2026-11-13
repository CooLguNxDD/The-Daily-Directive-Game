#include "DCGameState.h"

#include "DCEnvironment.h"
#include "DCVoxelCity.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 MaxBlastRecords = 400; // the reel re-stages any blast of the run
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
	if (ADCVoxelCity::IsEnabled())
	{
		VoxelCity = GetWorld()->SpawnActor<ADCVoxelCity>(ADCVoxelCity::StaticClass(), FTransform::Identity, Params);
	}
}

FDCVoxelOp& ADCGameState::AddVoxelOp(const FDCVoxelOp& Op, float ApplyTime)
{
	check(HasAuthority());
	FDCVoxelOp& Rec = VoxelOps.Add_GetRef(Op);
	Rec.Seq = NextVoxelSeq++;
	Rec.Build = CityBuildId;
	Rec.Sector = CitySector;
	Rec.Time = ApplyTime >= 0.f ? ApplyTime : Now();
	return Rec;
}

void ADCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCGameState, Blasts);
	DOREPLIFETIME(ADCGameState, Phase);
	DOREPLIFETIME(ADCGameState, SectorIndex);
	DOREPLIFETIME(ADCGameState, SectorEndTime);
	DOREPLIFETIME(ADCGameState, SectorStartTime);
	DOREPLIFETIME(ADCGameState, RunSeed);
	DOREPLIFETIME(ADCGameState, PhaseEndTime);
	DOREPLIFETIME(ADCGameState, bRunFailed);
	DOREPLIFETIME(ADCGameState, SectorPhotoPoints);
	DOREPLIFETIME(ADCGameState, QuotaResults);
	DOREPLIFETIME(ADCGameState, Collapses);
	DOREPLIFETIME(ADCGameState, Scoop);
	DOREPLIFETIME(ADCGameState, Mutators);
	DOREPLIFETIME(ADCGameState, ReelClips);
	DOREPLIFETIME(ADCGameState, ReelIndex);
	DOREPLIFETIME(ADCGameState, ReelStage);
	DOREPLIFETIME(ADCGameState, ReelClipStart);
	DOREPLIFETIME(ADCGameState, PagePicks);
	DOREPLIFETIME(ADCGameState, HeadlineIndex);
	DOREPLIFETIME(ADCGameState, Headline);
	DOREPLIFETIME(ADCGameState, Subhead);
	DOREPLIFETIME(ADCGameState, CitySector);
	DOREPLIFETIME(ADCGameState, CityBuildId);
	DOREPLIFETIME(ADCGameState, VoxelOps);
}

int32 ADCGameState::AddBlast(const FVector& Location, float Radius, FName StratagemId)
{
	check(HasAuthority());
	FDCBlastRecord& Rec = Blasts.AddDefaulted_GetRef();
	Rec.Id = NextBlastId++;
	Rec.Location = Location;
	Rec.Time = Now();
	Rec.TelegraphStart = Rec.Time;
	Rec.Radius = Radius;
	Rec.StratagemId = StratagemId;
	Rec.Sector = SectorIndex;
	if (Blasts.Num() > MaxBlastRecords)
	{
		Blasts.RemoveAt(0, Blasts.Num() - MaxBlastRecords);
	}
	return Rec.Id;
}

int32 ADCGameState::AddEvent(FName Kind, const FVector& Location, float Radius, float PeakTime, int32 LotIndex)
{
	const int32 Id = AddBlast(Location, Radius, NAME_None);
	FDCBlastRecord& Rec = Blasts.Last();
	Rec.Kind = Kind;
	Rec.TelegraphStart = Now();
	Rec.Time = PeakTime;
	Rec.LotIndex = LotIndex;
	return Id;
}

TArray<const FDCUpgradeDef*> ADCGameState::ActiveMutators() const
{
	TArray<const FDCUpgradeDef*> Out;
	for (const FName& Id : Mutators)
	{
		if (const FDCUpgradeDef* Def = UDCTuning::Get().FindUpgrade(Id))
		{
			Out.Add(Def);
		}
	}
	return Out;
}

const FDCBlastRecord* ADCGameState::FindBlast(int32 Id) const
{
	return Blasts.FindByPredicate([Id](const FDCBlastRecord& B) { return B.Id == Id; });
}

void ADCGameState::AddCollapse(int32 Sector, int32 Lot, const FVector& Epicenter, float Time)
{
	check(HasAuthority());
	FDCCollapseRecord& Rec = Collapses.AddDefaulted_GetRef();
	Rec.Sector = Sector;
	Rec.LotIndex = Lot;
	Rec.Time = Time >= 0.f ? Time : Now();
	Rec.Epicenter = Epicenter;
}
