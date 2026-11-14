// R4 objective director, host only: picks the sector's objective from data, places it (the package deep in the main
// faction's ground, or the marked towers), measures the crew each tick into FDCObjectiveInputs and steps the pure
// stage table (DCObjectiveRules::Next). Extraction pays the crew and ends the sector; a sector that times out fails
// its objective and goes on (the reel always plays). Part of ADCGameMode, kept apart like the faction director.
#include "DCGameMode.h"

#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCGameState.h"
#include "DCObjective.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "DCUnits.h"
#include "DCEnemy.h"
#include "DCVoxelCity.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rules/DCObjectiveRules.h"
#include "Misc/ScopeExit.h"

namespace
{
	void ToastAll(const TArray<ADCPlayerState*>& Players, const FString& Text)
	{
		for (ADCPlayerState* PS : Players)
		{
			if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
			{
				C->HostToast(Text);
			}
		}
	}
}

void ADCGameMode::StartObjective()
{
	if (Objective)
	{
		Objective->Destroy();
		Objective = nullptr;
	}
	ObjectiveTargets.Reset();
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	const FDCObjectiveDef* Def = DCObjectiveRules::Pick(T.Objectives, GS->RunSeed, GS->SectorIndex);
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("dcobjective="), Forced))
	{
		Def = T.Objectives.FindByPredicate([&Forced](const FDCObjectiveDef& D) { return D.Id == FName(*Forced); });
	}
	if (!Def || CityPlan.StreetPoints.Num() == 0)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Objective = GetWorld()->SpawnActor<ADCObjective>(ADCObjective::StaticClass(), FTransform::Identity, Params);
	Objective->DefId = Def->Id;
	Objective->Stage = EDCObjectiveStage::Active;
	const FVector Spawn = SpawnTransformFor(0).GetLocation();
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	auto Ground = [City](FVector P) { P.Z = City ? City->SurfaceZCm(P) : 0.f; return P; };
	// Retrieve: the package sits deep in the main faction's ground, the street point farthest from the crew's spawn.
	const int32 Deep = DCObjectiveRules::Deep(CityPlan.StreetPoints, Spawn, Def->DeepFraction, GS->RunSeed + GS->SectorIndex * 977);
	Objective->PackageAt = Ground(CityPlan.StreetPoints[FMath::Max(Deep, 0)]);
	// Destroy: marked standing towers, far from the spawn and apart from each other.
	if (Def->Targets > 0 && Def->Kind == TEXT("Destroy"))
	{
		TArray<ADCBuilding*> Towers;
		TArray<FVector> At;
		for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
		{
			if (!B->IsCollapsed())
			{
				Towers.Add(B);
				At.Add(B->GetActorLocation());
			}
		}
		// Actor iteration order is not a contract: sort by lot so every machine and replay picks the same towers.
		TArray<int32> Order;
		for (int32 i = 0; i < Towers.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.Sort([&Towers](int32 A, int32 B) { return Towers[A]->GetLotIndex() < Towers[B]->GetLotIndex(); });
		TArray<ADCBuilding*> Sorted;
		TArray<FVector> SortedAt;
		for (int32 i : Order)
		{
			Sorted.Add(Towers[i]);
			SortedAt.Add(At[i]);
		}
		for (int32 i : DCObjectiveRules::PickTargets(SortedAt, Spawn, Def->Targets, Def->TargetSpacingCm, GS->RunSeed + GS->SectorIndex * 131))
		{
			const FDCBuildingSpec& S = Sorted[i]->GetSpec();
			ObjectiveTargets.Add(Sorted[i]);
			Objective->TargetAt.Add(SortedAt[i] + FVector(0, 0, S.Floors * S.BlockSize));
			Objective->TargetDown.Add(0);
		}
	}
	NextGuardTime = 0.f;
	GuardWavesSent = 0;
	UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE sector=%d %s (%s) package=%s targets=%d"), GS->SectorIndex + 1, *Def->Id.ToString(), *Def->Kind.ToString(),
		*Objective->PackageAt.ToCompactString(), Objective->TargetAt.Num());
}

void ADCGameMode::TickObjective(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const FDCObjectiveDef* Def = Objective ? Objective->GetDef() : nullptr;
	if (!Def || (Objective->Stage != EDCObjectiveStage::Active && Objective->Stage != EDCObjectiveStage::Extract))
	{
		return;
	}
	const TArray<ADCPlayerState*> All = Players();
	TArray<ADCCharacter*> Standing;
	for (ADCPlayerState* PS : All)
	{
		ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		if (C && !C->IsDowned())
		{
			Standing.Add(C);
		}
	}
	const bool bRetrieve = Def->Kind == TEXT("Retrieve");
	// The package: a downed carrier drops it where they fell; a standing player close by picks it up.
	if (bRetrieve)
	{
		const ADCCharacter* Bearer = Objective->Carrier ? Objective->Carrier->GetPawn<ADCCharacter>() : nullptr;
		if (Objective->Carrier && (!Bearer || Bearer->IsDowned()))
		{
			Objective->PackageAt = Bearer ? Bearer->GetActorLocation() - FVector(0, 0, 80.f) : FVector(Objective->PackageAt);
			Objective->Carrier = nullptr;
			ToastAll(All, TEXT("THE PACKAGE IS DOWN. PICK IT UP."));
		}
		else if (Bearer)
		{
			Objective->PackageAt = Bearer->GetActorLocation();
		}
		for (ADCCharacter* C : Standing)
		{
			if (!Objective->Carrier && FVector::Dist2D(C->GetActorLocation(), Objective->PackageAt) < Def->PickupRadiusCm)
			{
				Objective->Carrier = C->GetPlayerState();
				ToastAll(All, FString::Printf(TEXT("%s HAS THE PACKAGE"), *C->GetPlayerState()->GetPlayerName().ToUpper()));
			}
		}
		// The deep swarm: as the crew closes in on a package nobody carries, the main faction erupts around it.
		const bool bNear = Standing.ContainsByPredicate([&](const ADCCharacter* C) { return FVector::Dist2D(C->GetActorLocation(), Objective->PackageAt) < Def->GuardTriggerCm; });
		if (!Objective->Carrier && bNear && Now >= NextGuardTime && FactionMode() && GuardWavesSent < Def->GuardWaves)
		{
			NextGuardTime = Now + Def->GuardGapSeconds;
			++GuardWavesSent;
			const int32 Landed = SpawnSquad(GS->FactionMix.Main, Def->GuardSquad, Objective->PackageAt, Def->GuardSpreadCm);
			UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE guards %s landed=%d"), *GS->FactionMix.Main.ToString(), Landed);
		}
	}
	// Destroy: a marked tower counts once it is down, whatever brought it down (strike, carve, faction fire).
	FDCObjectiveInputs In;
	for (int32 i = 0; i < ObjectiveTargets.Num(); ++i)
	{
		const ADCBuilding* B = ObjectiveTargets[i].Get();
		// Strikes and carving count (R4): the gameplay collapse record or the voxel city's committed fall of the lot.
		const ADCVoxelCity* TargetCity = ADCVoxelCity::Get(this);
		const bool bDown = !B || B->IsCollapsed() || (TargetCity && TargetCity->IsTowerFallen(B->GetLotIndex()));
		if (bDown && !Objective->TargetDown[i])
		{
			Objective->TargetDown[i] = 1;
			UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE target %d down (lot %d, collapsed=%d voxel-fallen=%d)"), i, B ? B->GetLotIndex() : -1, B && B->IsCollapsed() ? 1 : 0,
				B && TargetCity && TargetCity->IsTowerFallen(B->GetLotIndex()) ? 1 : 0);
			ToastAll(All, FString::Printf(TEXT("TARGET DOWN  %d / %d"), ObjectiveTargets.Num() - Objective->TargetsLeft(), ObjectiveTargets.Num()));
		}
		In.Destroyed += bDown ? 1 : 0;
	}
	In.Targets = ObjectiveTargets.Num();
	In.bCarried = Objective->Carrier != nullptr;
	const ADCCharacter* Bearer = Objective->Carrier ? Objective->Carrier->GetPawn<ADCCharacter>() : nullptr;
	In.bCarrierInZone = Bearer && FVector::Dist2D(Bearer->GetActorLocation(), Objective->ExtractAt) < Def->ExtractRadiusCm;
	In.bAnyoneInZone = Standing.ContainsByPredicate([&](const ADCCharacter* C) { return FVector::Dist2D(C->GetActorLocation(), Objective->ExtractAt) < Def->ExtractRadiusCm; });
	const EDCObjectiveStage Was = Objective->Stage;
	const EDCObjectiveStage Now2 = DCObjectiveRules::Next(Def->Kind, Was, In);
	if (Now2 == Was)
	{
		return;
	}
	Objective->Stage = Now2;
	if (Now2 == EDCObjectiveStage::Extract)
	{
		// The beacon lands well away from the work: the crew has to cross the map with it.
		const FVector From = bRetrieve ? FVector(Objective->PackageAt) : (Objective->TargetAt.Num() > 0 ? FVector(Objective->TargetAt[0]) : FVector::ZeroVector);
		const int32 Pick = DCObjectiveRules::AtLeast(CityPlan.StreetPoints, From, Def->ExtractMinDistanceCm);
		FVector At = CityPlan.StreetPoints[FMath::Max(Pick, 0)];
		const ADCVoxelCity* City = ADCVoxelCity::Get(this);
		At.Z = City ? City->SurfaceZCm(At) : 0.f;
		Objective->ExtractAt = At;
		ToastAll(All, Def->ExtractToast);
		UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE extract open at %s"), *At.ToCompactString());
		return;
	}
	if (Now2 == EDCObjectiveStage::Done)
	{
		for (ADCPlayerState* PS : All)
		{
			PS->AddPoints(Def->RewardPoints);
		}
		ToastAll(All, Def->DoneToast);
		GS->SectorEndTime = FMath::Min(GS->SectorEndTime, Now + Def->ExtractEndSeconds);
		UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE done: extracted, sector ends at %.1f"), GS->SectorEndTime);
	}
}

void ADCGameMode::EndObjective()
{
	const FDCObjectiveDef* Def = Objective ? Objective->GetDef() : nullptr;
	if (!Def)
	{
		return;
	}
	ON_SCOPE_EXIT { GetGameState<ADCGameState>()->ObjectiveResults.Add(Objective->Stage == EDCObjectiveStage::Done ? 1 : 0); };
	if (Objective->Stage == EDCObjectiveStage::Active || Objective->Stage == EDCObjectiveStage::Extract)
	{
		FDCObjectiveInputs In;
		In.bTimeUp = true;
		Objective->Stage = DCObjectiveRules::Next(Def->Kind, Objective->Stage, In);
		ToastAll(Players(), Def->FailToast);
		UE_LOG(LogTemp, Display, TEXT("DCOBJECTIVE failed: time up"));
	}
}
