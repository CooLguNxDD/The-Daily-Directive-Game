// R3 faction director, host only: rolls each map's faction mix from the run seed, drops squads for the pressure rule
// (main faction near the crew, invaders from the far edge), sends the friendly army on its own, and lets factions
// call their own strikes on towers (advisor a-2: through CallStrike, rate-limited, so faction destruction is graded,
// replicated and deterministic like the crew's). Part of ADCGameMode, kept apart to keep the run director readable.
#include "DCGameMode.h"

#include "DCBuilding.h"
#include "DCCharacter.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCPlayerState.h"
#include "DCTuning.h"
#include "DCUnits.h"
#include "DCVoxelCity.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rules/DCFactionRules.h"

bool ADCGameMode::FactionMode() const
{
	const ADCGameState* GS = GetGameState<ADCGameState>();
	return GS && !GS->FactionMix.Main.IsNone();
}

void ADCGameMode::RollFactions()
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	GS->FactionMix = DCFactionRules::RollMix(T.Factions, T.FactionMix, GS->RunSeed, GS->SectorIndex);
	// Evidence runs: -dcmain=<Faction> forces the owner (its invaders stay rolled).
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("dcmain="), Forced) && DCFactionRules::Find(T.Factions, FName(*Forced)))
	{
		GS->FactionMix.Invaders.Remove(FName(*Forced));
		GS->FactionMix.Main = FName(*Forced);
	}
	NextAllyTime = GetWorld()->GetTimeSeconds() + T.FactionMix.AllyGapSeconds * 0.5f;
	const int32 Side = FMath::Max(1, T.FactionMix.ZonesPerSide);
	GS->Zones.Init(GS->FactionMix.Main, Side * Side);
	GS->ZoneHalfExtent = CityPlan.HalfExtent;
	NextTerritoryTime = 0.f;
	bOpeningDone = false;
	bNestsDone = false;
	NextFactionStrike = 0.f;
	FactionStrikeReady.Reset();
	FString Invaders;
	for (const FName& I : GS->FactionMix.Invaders)
	{
		Invaders += I.ToString() + TEXT(" ");
	}
	UE_LOG(LogTemp, Display, TEXT("DCFACTION sector=%d main=%s (share %.2f) invaders=%s allies=%d"), GS->SectorIndex + 1, *GS->FactionMix.Main.ToString(),
		GS->FactionMix.MainShare, *Invaders, GS->FactionMix.Allies.Num());
}

FVector ADCGameMode::DropPoint(const FVector& Near, bool bFar, const FDCPressureTuning& P, FName DropFaction)
{
	// Main faction and allies land in a ring around the crew; invaders cross in from beyond it.
	TArray<FVector> Pool;
	for (const FVector& Pt : CityPlan.StreetPoints)
	{
		const float Dist = FVector::Dist2D(Pt, Near);
		const bool bIn = bFar ? Dist > P.RingMaxCm : (Dist >= P.RingMinCm && Dist <= P.RingMaxCm);
		if (bIn)
		{
			Pool.Add(Pt);
		}
	}
	const TArray<FVector>& From = Pool.Num() > 0 ? Pool : CityPlan.StreetPoints;
	// Ground a faction holds is where its reinforcements come in (territory matters for the next drops).
	const ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	if (!DropFaction.IsNone() && GS && GS->Zones.Contains(DropFaction))
	{
		TArray<FVector> Held;
		for (const FVector& Pt : From)
		{
			const int32 Z = DCFactionRules::ZoneOf(Pt, CityPlan.HalfExtent, T.FactionMix.ZonesPerSide);
			if (GS->Zones.IsValidIndex(Z) && GS->Zones[Z] == DropFaction)
			{
				Held.Add(Pt);
			}
		}
		if (Held.Num() > 0)
		{
			return Held[SectorRng.RandRange(0, Held.Num() - 1)];
		}
	}
	return From.Num() > 0 ? From[SectorRng.RandRange(0, From.Num() - 1)] : Near;
}

int32 ADCGameMode::SpawnSquad(FName Faction, int32 Count, const FVector& Centre, float Spread)
{
	const UDCTuning& T = UDCTuning::Get();
	const FDCFactionDef* F = DCFactionRules::Find(T.Factions, Faction);
	if (!F || Count <= 0)
	{
		return 0;
	}
	const ADCVoxelCity* City = ADCVoxelCity::Get(this);
	const int32 Squad = NextSquadId++;
	int32 Landed = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const FDCEnemyDef* Def = T.FindEnemy(DCFactionRules::PickUnit(*F, SectorRng.FRand()));
		if (!Def)
		{
			continue;
		}
		FVector Where = Centre + FVector(SectorRng.FRandRange(-Spread, Spread), SectorRng.FRandRange(-Spread, Spread), 0.f);
		Where.Z = (City ? City->SurfaceZCm(Where) : 0.f) + Def->HoverHeight;
		const FTransform Xf(FRotator(0, SectorRng.FRandRange(0, 360), 0), Where);
		PendingSpawns.Add({Def->Id, Xf, Squad, i, Count});
		++Landed;
	}
	return Landed;
}

void ADCGameMode::TickSpawns()
{
	const int32 N = FMath::Min(PendingSpawns.Num(), FMath::Max(1, UDCTuning::Get().FactionMix.SpawnsPerFrame));
	for (int32 i = 0; i < N; ++i)
	{
		const FPendingSpawn& P = PendingSpawns[i];
		// Never inside a tower: a spot that would collide is nudged clear or skipped.
		ADCEnemy* E = GetWorld()->SpawnActorDeferred<ADCEnemy>(ADCEnemy::StaticClass(), P.Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (E)
		{
			E->InitKind(P.Kind);
			E->HostSetSquad(P.Squad, P.Slot, P.Size);
			E->FinishSpawning(P.Xf);
		}
	}
	PendingSpawns.RemoveAt(0, N, EAllowShrinking::No);
}

void ADCGameMode::TickFactions(float Now)
{
	ADCGameState* GS = GetGameState<ADCGameState>();
	const UDCTuning& T = UDCTuning::Get();
	if (!FactionMode() || CityPlan.StreetPoints.Num() == 0 || !T.Sectors.IsValidIndex(GS->SectorIndex))
	{
		return;
	}
	const FDCPressureTuning& P = T.Sectors[GS->SectorIndex].Pressure;
	TArray<ADCCharacter*> Standing;
	for (ADCPlayerState* PS : Players())
	{
		ADCCharacter* C = PS->GetPawn<ADCCharacter>();
		if (C && !C->IsDowned())
		{
			Standing.Add(C);
		}
	}
	// Nests: factions that nest (the Hive) burst up their nests in the ring around the crew, once per sector.
	if (!bNestsDone && Standing.Num() > 0 && Now >= SectorStartTime + T.FactionMix.OpeningSeconds)
	{
		bNestsDone = true;
		TArray<FName> OnMap = GS->FactionMix.Invaders;
		OnMap.Add(GS->FactionMix.Main);
		for (const FName& Id : OnMap)
		{
			const FDCFactionDef* F = DCFactionRules::Find(T.Factions, Id);
			const FDCEnemyDef* NestDef = F && F->NestCount > 0 ? T.FindEnemy(F->NestUnit) : nullptr;
			for (int32 n = 0; NestDef && n < F->NestCount; ++n)
			{
				FVector At = DropPoint(Standing[0]->GetActorLocation(), Id != GS->FactionMix.Main, P);
				const ADCVoxelCity* NestCity = ADCVoxelCity::Get(this);
				At.Z = (NestCity ? NestCity->SurfaceZCm(At) : 0.f) + NestDef->HoverHeight;
				PendingSpawns.Add({NestDef->Id, FTransform(FRotator(0, SectorRng.FRandRange(0, 360), 0), At), NextSquadId++, 0, 1});
				UE_LOG(LogTemp, Display, TEXT("DCFACTION nest %s at %s"), *Id.ToString(), *At.ToCompactString());
			}
		}
	}
	// The opening clash: a fight already under way in sight of the crew within seconds of landing.
	if (!bOpeningDone && Standing.Num() > 0 && T.FactionMix.OpeningSquad > 0 && Now >= SectorStartTime + T.FactionMix.OpeningSeconds)
	{
		bOpeningDone = true;
		const FName Raider = GS->FactionMix.Invaders.Num() > 0 ? GS->FactionMix.Invaders[0] : (GS->FactionMix.Allies.Num() > 0 ? GS->FactionMix.Allies[0] : NAME_None);
		const FVector A = DropPoint(Standing[0]->GetActorLocation(), false, P);
		// The other side lands on the street point nearest OpeningApartCm away from the first.
		FVector B = A;
		float BestGap = TNumericLimits<float>::Max();
		for (const FVector& Pt : CityPlan.StreetPoints)
		{
			const float Gap = FMath::Abs(FVector::Dist2D(Pt, A) - T.FactionMix.OpeningApartCm);
			B = Gap < BestGap ? Pt : B;
			BestGap = FMath::Min(BestGap, Gap);
		}
		const int32 Main = SpawnSquad(GS->FactionMix.Main, T.FactionMix.OpeningSquad, A, 300.f);
		const int32 Other = SpawnSquad(Raider, T.FactionMix.OpeningSquad, B, 300.f);
		UE_LOG(LogTemp, Display, TEXT("DCFACTION opening clash %s x%d vs %s x%d"), *GS->FactionMix.Main.ToString(), Main, *Raider.ToString(), Other);
	}
	if (Standing.Num() == 0 || Now < SectorStartTime + P.StartSeconds)
	{
		return;
	}
	UDCUnits* Units = UDCUnits::Get(this);
	// Territory: zones go to whoever holds the ground in numbers (allies too).
	if (Units && Now >= NextTerritoryTime)
	{
		NextTerritoryTime = Now + T.FactionMix.TerritorySeconds;
		TArray<TPair<FVector, FName>> Live;
		for (const ADCEnemy* U : Units->All())
		{
			if (U && U->IsAlive())
			{
				Live.Emplace(U->GetActorLocation(), U->GetFaction());
			}
		}
		const TArray<FName> Before = GS->Zones;
		if (DCFactionRules::StepTerritory(GS->Zones, Live, CityPlan.HalfExtent, T.FactionMix.ZonesPerSide, T.FactionMix.TakeMinUnits) > 0)
		{
			for (int32 z = 0; z < GS->Zones.Num(); ++z)
			{
				if (Before.IsValidIndex(z) && Before[z] != GS->Zones[z])
				{
					UE_LOG(LogTemp, Display, TEXT("DCTERRITORY zone %d: %s -> %s"), z, *Before[z].ToString(), *GS->Zones[z].ToString());
				}
			}
		}
	}
	// The friendly army: its own squads on its own clock, no orders (GOAL: acts on its own).
	if (T.FactionMix.AllyGapSeconds > 0.f && Now >= NextAllyTime && GS->FactionMix.Allies.Num() > 0 && Units)
	{
		NextAllyTime = Now + T.FactionMix.AllyGapSeconds;
		const FName Ally = GS->FactionMix.Allies[SectorRng.RandRange(0, GS->FactionMix.Allies.Num() - 1)];
		const FDCFactionDef* F = DCFactionRules::Find(T.Factions, Ally);
		const int32 Room = T.FactionMix.AllyMaxAlive - Units->CountAlive(Ally);
		if (F && Room > 0)
		{
			const FVector Near = Standing[SectorRng.RandRange(0, Standing.Num() - 1)]->GetActorLocation();
			const int32 N = FMath::Min(Room, SectorRng.RandRange(F->SquadMin, F->SquadMax));
			const int32 Landed = SpawnSquad(Ally, N, DropPoint(Near, false, P, Ally), 350.f);
			UE_LOG(LogTemp, Display, TEXT("DCFACTION ally squad %s landed=%d"), *Ally.ToString(), Landed);
		}
	}
	// Faction strikes: one at a time across all factions, each faction on its own interval, never while a big
	// collapse is still settling (the body budget belongs to the crew's shots first).
	const ADCVoxelCity* StrikeCity = ADCVoxelCity::Get(this);
	if (Now < NextFactionStrike || Now < SectorStartTime + T.FactionMix.StrikeStartSeconds || !Units || (StrikeCity && StrikeCity->BodyLoad() > T.FactionMix.StrikeMaxBodyLoad))
	{
		return;
	}
	TArray<FName> Present;
	Present.Add(GS->FactionMix.Main);
	Present.Append(GS->FactionMix.Invaders);
	Present.Append(GS->FactionMix.Allies);
	for (const FName& Id : Present)
	{
		const FDCFactionDef* F = DCFactionRules::Find(T.Factions, Id);
		const FDCStratagemDef* Strike = F && F->StrikeInterval > 0.f ? T.FindStratagem(F->StrikeStratagem) : nullptr;
		if (!Strike || Now < FactionStrikeReady.FindRef(Id))
		{
			continue;
		}
		// The squad's centre: where this faction's live units stand.
		FVector Sum = FVector::ZeroVector;
		int32 N = 0;
		for (const ADCEnemy* U : Units->All())
		{
			if (U && U->IsAlive() && U->GetFaction() == Id)
			{
				Sum += U->GetActorLocation();
				++N;
			}
		}
		if (N == 0)
		{
			continue;
		}
		const FVector Centre = Sum / N;
		const ADCBuilding* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (ADCBuilding* B : TActorRange<ADCBuilding>(GetWorld()))
		{
			const float D = B->IsCollapsed() ? BestD : FVector::DistSquared2D(B->GetActorLocation(), Centre);
			Best = D < BestD ? B : Best;
			BestD = FMath::Min(BestD, D);
		}
		if (!Best)
		{
			continue;
		}
		const FVector Target = Best->GetActorLocation();
		CallStrike(*Strike, Target, nullptr);
		FactionStrikeReady.Add(Id, Now + F->StrikeInterval);
		NextFactionStrike = Now + T.FactionMix.StrikeGapSeconds;
		for (ADCPlayerState* PS : Players())
		{
			if (ADCCharacter* C = PS->GetPawn<ADCCharacter>())
			{
				const float Metres = FVector::Dist2D(C->GetActorLocation(), Target) / 100.f;
				C->HostToast(F->StrikeToast.Replace(TEXT("{M}"), *FString::Printf(TEXT("%.0f"), Metres)));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("DCFACTION strike %s (%s) on tower at %s, %d units"), *Id.ToString(), *Strike->Id.ToString(), *Target.ToCompactString(), N);
		return;
	}
}
