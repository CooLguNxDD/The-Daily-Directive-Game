#include "DCGameState.h"
#include "DCBuilding.h"
#include "DCObjective.h"
#include "EngineUtils.h"
#include "Rules/DCTowerHealth.h"
#include "Voxel/DCVoxelSettings.h"

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
	DOREPLIFETIME(ADCGameState, FactionMix);
	DOREPLIFETIME(ADCGameState, Zones);
	DOREPLIFETIME(ADCGameState, ZoneHalfExtent);
	DOREPLIFETIME(ADCGameState, ObjectiveResults);
	DOREPLIFETIME(ADCGameState, SectorEndTime);
	DOREPLIFETIME(ADCGameState, SectorStartTime);
	DOREPLIFETIME(ADCGameState, RunSeed);
	DOREPLIFETIME(ADCGameState, PhaseEndTime);
	DOREPLIFETIME(ADCGameState, bRunFailed);
	DOREPLIFETIME(ADCGameState, CrewLives);
	DOREPLIFETIME(ADCGameState, SectorPhotoPoints);
	DOREPLIFETIME(ADCGameState, QuotaResults);
	DOREPLIFETIME(ADCGameState, TowerHealthLeft);
	DOREPLIFETIME(ADCGameState, Collapses);
	DOREPLIFETIME(ADCGameState, Scoop);
	DOREPLIFETIME(ADCGameState, Mutators);
	DOREPLIFETIME(ADCGameState, ReelClips);
	DOREPLIFETIME(ADCGameState, ReelIndex);
	DOREPLIFETIME(ADCGameState, ReelStage);
	DOREPLIFETIME(ADCGameState, ReelClipStart);
	DOREPLIFETIME(ADCGameState, bReelClipLive);
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

void ADCGameState::OnRep_FactionMix()
{
	// Co-op evidence: the client logs the mix it received (the host logs DCFACTION when it rolls it).
	FString Invaders;
	for (const FName& I : FactionMix.Invaders)
	{
		Invaders += I.ToString() + TEXT(" ");
	}
	UE_LOG(LogTemp, Display, TEXT("DCFACTION client sector=%d main=%s invaders=%s"), SectorIndex + 1, *FactionMix.Main.ToString(), *Invaders);
}

TArray<int32> ADCGameState::HostStrikeTowers(const FVector& Center, float BlastRadius, float Damage)
{
	const FDCVoxelTuning& Tn = UDCVoxelSettings::Get().Tuning;
	TArray<int32> Holds;
	if (!Tn.bTowerHealth)
	{
		return Holds;
	}
	// The ledger belongs to one city build: a rebuilt sector starts every tower at full health.
	if (HostTowerBuild != CityBuildId)
	{
		HostTowerBuild = CityBuildId;
		HostTowerDamage.Reset();
		HostTowerHealth.Reset();
		TowerHealthLeft.Reset();
	}
	// HQ-marked towers (Destroy) are softer: the lots nearest the objective's markers.
	TArray<int32> Targets;
	for (const ADCObjective* O : TActorRange<ADCObjective>(GetWorld()))
	{
		for (const FVector_NetQuantize& Mark : O->TargetAt)
		{
			const ADCBuilding* Near = nullptr;
			float Best = TNumericLimits<float>::Max();
			for (const ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
			{
				const float D = FVector::Dist2D(B->GetCenter(), Mark);
				Near = D < Best ? B : Near;
				Best = FMath::Min(Best, D);
			}
			if (Near)
			{
				Targets.Add(Near->GetLotIndex());
			}
		}
	}
	for (const ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
	{
		const int32 Lot = B->GetLotIndex();
		if (Lot < 0 || B->IsCollapsed())
		{
			continue;
		}
		const FDCBuildingSpec& S = B->GetSpec();
		const FVector C = B->GetCenter();
		const float Dx = FMath::Max(FMath::Abs(Center.X - C.X) - S.SizeX * S.BlockSize * 0.5f, 0.f);
		const float Dy = FMath::Max(FMath::Abs(Center.Y - C.Y) - S.SizeY * S.BlockSize * 0.5f, 0.f);
		const float Health = DCTowerHealth::Health(S.Floors, S.SizeX, S.SizeY, Tn.TowerHealthPerFloor, Tn.TowerHealthMin, Tn.TowerHealthMax)
			* (Targets.Contains(Lot) ? Tn.TowerHealthTargetScale : 1.f);
		if (HostTowerDamage.Num() <= Lot)
		{
			HostTowerDamage.SetNumZeroed(Lot + 1);
			HostTowerHealth.SetNumZeroed(Lot + 1);
			TowerHealthLeft.SetNum(Lot + 1);
		}
		HostTowerHealth[Lot] = Health;
		const float Hit = DCTowerHealth::Damage(Damage, FMath::Sqrt(Dx * Dx + Dy * Dy), BlastRadius, Tn.TowerDamageEdge);
		HostTowerDamage[Lot] += Hit;
		TowerHealthLeft[Lot] = FMath::Clamp(1.f - HostTowerDamage[Lot] / FMath::Max(Health, 1.f), 0.f, 1.f);
		if (HostTowerDamage[Lot] < Health)
		{
			Holds.Add(Lot);
		}
		if (Hit > 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("DCTOWERHP lot %d hit %.0f -> %.0f / %.0f (%s)"), Lot, Hit, HostTowerDamage[Lot], Health, HostTowerDamage[Lot] < Health ? TEXT("holds") : TEXT("may fall"));
		}
	}
	ForceNetUpdate();
	return Holds;
}

bool ADCGameState::HostTowerSpent(int32 Lot) const
{
	if (!UDCVoxelSettings::Get().Tuning.bTowerHealth || HostTowerBuild != CityBuildId)
	{
		return !UDCVoxelSettings::Get().Tuning.bTowerHealth;
	}
	return HostTowerHealth.IsValidIndex(Lot) && HostTowerHealth[Lot] > 0.f && HostTowerDamage[Lot] >= HostTowerHealth[Lot];
}
