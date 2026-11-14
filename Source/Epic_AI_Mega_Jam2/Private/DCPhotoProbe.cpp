#include "DCPhotoProbe.h"

#include "DCCharacter.h"
#include "GameFramework/PlayerState.h"

#include "Algo/Find.h"
#include "DCBuilding.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCOrbitalStrike.h"
#include "DCTuning.h"
#include "DCVoxelCity.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const FName MCollapses(TEXT("Collapses"));
	const FName MDebris(TEXT("Debris"));
	const FName MEnemies(TEXT("Enemies"));
	const FName MWalkers(TEXT("Walkers"));
	/** R3: distinct factions with a live unit in frame (a CLASH when two or more fight in one shot). */
	const FName MFactions(TEXT("Factions"));
	const FName MBlasts(TEXT("Blasts"));
	const FName MDanger(TEXT("DangerMeters"));
	const FName MBlastMeters(TEXT("BlastMeters"));
	const FName MCentering(TEXT("Centering"));

	constexpr float BlastMemorySeconds = 4.f;
	constexpr float CollapseAirborneSeconds = 3.f;
	constexpr float FireballHeightCm = 600.f;

	float VisibleFraction(UWorld* World, const FVector& From, const TArray<FVector>& Points, const AActor* Ignore, const TArray<int32>& SubjectLots = {})
	{
		if (Points.Num() == 0)
		{
			return 0.f;
		}
		if (const ADCVoxelCity* City = ADCVoxelCity::Get(World))
		{
			// Voxel city: sight marches the voxels (the subject's own lots never hide it), not physics collision.
			int32 Clear = 0;
			for (const FVector& P : Points)
			{
				Clear += City->IsSightBlocked(From, P, SubjectLots) ? 0 : 1;
			}
			return static_cast<float>(Clear) / Points.Num();
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DCPhotoOcclusion), false, Ignore);
		int32 Seen = 0;
		for (const FVector& P : Points)
		{
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, From, P, ECC_Visibility, Params);
			Seen += (!bBlocked || FVector::DistSquared(Hit.ImpactPoint, P) < FMath::Square(250.f)) ? 1 : 0;
		}
		return static_cast<float>(Seen) / Points.Num();
	}

	float Centering(const FVector2D& Ndc) { return FMath::Clamp(1.f - Ndc.Size() / 1.2f, 0.f, 1.f); }

	void BlastSamples(const FDCBlastRecord& B, TArray<FVector>& Out)
	{
		const FVector C = B.Location + FVector(0, 0, FireballHeightCm);
		const float R = FMath::Max(B.Radius * 0.25f, 150.f);
		Out = {C, C + FVector(R, 0, 0), C - FVector(R, 0, 0), C + FVector(0, R, 0), C - FVector(0, R, 0), C + FVector(0, 0, R)};
		// The beam column up to tower height: the strike reads from the street even when its fireball sits in a shell.
		for (const float Z : {600.f, 1500.f, 3000.f})
		{
			Out.Add(B.Location + FVector(0, 0, Z));
		}
	}
}

namespace DCPhotoProbe
{
	bool ProjectToFrame(const FDCShotView& View, const FVector& P, FVector2D& OutNdc)
	{
		const FVector Local = View.Rotation.UnrotateVector(P - View.Location);
		if (Local.X <= 1.f)
		{
			return false;
		}
		const float TanH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.FOV, 5.f, 170.f) * 0.5f));
		const float TanV = TanH / FMath::Max(View.Aspect, 0.1f);
		OutNdc = FVector2D(Local.Y / Local.X / TanH, Local.Z / Local.X / TanV);
		return true;
	}

	bool InFrame(const FDCShotView& View, const FVector& P, float Margin)
	{
		FVector2D Ndc;
		return ProjectToFrame(View, P, Ndc) && FMath::Abs(Ndc.X) <= Margin && FMath::Abs(Ndc.Y) <= Margin;
	}

	FDCPhotoInputs Measure(UWorld* World, const FDCShotView& View, float ShotTime, const AActor* Photographer, bool bSpotlit, int32& OutBlastId)
	{
		FDCPhotoInputs In;
		In.bSpotlit = bSpotlit;
		OutBlastId = 0;
		const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
		if (!GS)
		{
			return In;
		}
		const float TagRange = UDCTuning::Get().Camera.TagRangeCm;
		float Danger = TNumericLimits<float>::Max();
		float NearestBlast = TNumericLimits<float>::Max();
		float BestSubject = -1.f;
		int32 BlastsInFrame = 0;
		TArray<FVector> Samples;

		// Blasts: the subject and the timing.
		for (const FDCBlastRecord& B : GS->Blasts)
		{
			const float Age = ShotTime - B.Time;
			if (Age < -0.3f || Age > BlastMemorySeconds)
			{
				continue;
			}
			const FVector C = B.Location + FVector(0, 0, FireballHeightCm);
			const float Dist = FVector::Dist(View.Location, B.Location);
			Danger = FMath::Min(Danger, Dist);
			NearestBlast = FMath::Min(NearestBlast, Dist);
			// In frame when the fireball centre or any of its samples (the beam, the upper blast) projects inside (CP7:
			// framing the visible upper blast without the centre lost its timing bonus).
			BlastSamples(B, Samples);
			auto InFrame = [&View](const FVector& P, FVector2D& Out) { return ProjectToFrame(View, P, Out) && FMath::Abs(Out.X) <= 1.1f && FMath::Abs(Out.Y) <= 1.1f; };
			FVector2D Ndc;
			bool bInFrame = InFrame(C, Ndc);
			for (int32 i = 0; i < Samples.Num() && !bInFrame; ++i)
			{
				bInFrame = InFrame(Samples[i], Ndc);
			}
			if (!bInFrame)
			{
				continue;
			}
			TArray<int32> Struck;
			if (const ADCVoxelCity* City = ADCVoxelCity::Get(World))
			{
				City->LotsNear(B.Location, B.Radius, Struck); // the towers this blast strikes are its subject, never its cover
			}
			const float Vis = VisibleFraction(World, View.Location, Samples, Photographer, Struck);
			if (Vis <= 0.f)
			{
				continue;
			}
			BlastsInFrame += Age <= 1.5f ? 1 : 0;
			const float Subject = Centering(Ndc) * Vis + (Age >= 0.f ? 0.5f : 0.f) - Age * 0.05f;
			if (Subject > BestSubject)
			{
				BestSubject = Subject;
				In.bHasBlast = true;
				In.SecondsSinceBlast = Age;
				In.Centering = Centering(Ndc);
				In.Visibility = Vis;
				OutBlastId = B.Id;
			}
		}

		// Collapsing towers: DEMOLITION and airborne debris (estimated from the host's collapse time, not pieces).
		int32 Collapses = 0;
		float Debris = 0.f;
		for (ADCBuilding* Building : TActorRange<ADCBuilding>(World))
		{
			if (!Building->IsCollapsed())
			{
				continue;
			}
			const float Age = ShotTime - Building->GetCollapseTime();
			if (Age < -0.05f || Age > CollapseAirborneSeconds)
			{
				continue;
			}
			const FVector Center = Building->GetActorLocation() + FVector(0, 0, Building->GetHeight() * 0.4f);
			Danger = FMath::Min(Danger, FVector::Dist(View.Location, Center));
			// The falling mass spans the tower's whole height: any of these in frame and in sight counts.
			FVector2D Ndc;
			bool bFramed = false;
			Samples.Reset();
			for (const float Frac : {0.1f, 0.4f, 0.7f})
			{
				const FVector P = Building->GetActorLocation() + FVector(0, 0, Building->GetHeight() * Frac);
				FVector2D PNdc;
				if (ProjectToFrame(View, P, PNdc) && FMath::Abs(PNdc.X) <= 1.15f && FMath::Abs(PNdc.Y) <= 1.15f)
				{
					Ndc = bFramed ? Ndc : PNdc;
					bFramed = true;
					Samples.Add(P);
				}
			}
			if (!bFramed || VisibleFraction(World, View.Location, Samples, Photographer, {Building->GetLotIndex()}) <= 0.f)
			{
				continue;
			}
			++Collapses;
			const FDCBuildingSpec& Spec = Building->GetSpec();
			Debris += Spec.Floors * (Spec.SizeX + Spec.SizeY) * 2.f * FMath::Clamp(1.f - Age / CollapseAirborneSeconds, 0.f, 1.f) * 0.25f;
			if (!In.bHasBlast && BestSubject < 0.f)
			{
				In.Centering = Centering(Ndc);
				In.Visibility = 1.f;
				BestSubject = 0.f;
			}
		}

		// Enemies in frame and in reach.
		int32 Enemies = 0;
		int32 Walkers = 0;
		TSet<FName> Factions;
		for (ADCEnemy* Enemy : TActorRange<ADCEnemy>(World))
		{
			if (!Enemy->IsAlive())
			{
				continue;
			}
			const FVector P = Enemy->GetActorLocation();
			const float Dist = FVector::Dist(View.Location, P);
			Danger = FMath::Min(Danger, Dist);
			if (Dist > TagRange || !InFrame(View, P, 1.f))
			{
				continue;
			}
			Samples.Reset();
			Samples.Add(P);
			if (VisibleFraction(World, View.Location, Samples, Photographer) <= 0.f)
			{
				continue;
			}
			++Enemies;
			Walkers += Enemy->IsWalker() ? 1 : 0;
			Factions.Add(Enemy->GetFaction());
		}

		// Strikes still telegraphing count as danger (standing under a beam is dramatic).
		for (ADCOrbitalStrike* Strike : TActorRange<ADCOrbitalStrike>(World))
		{
			if (Strike->GetDetonateTime() > ShotTime)
			{
				Danger = FMath::Min(Danger, FVector::Dist(View.Location, Strike->GetActorLocation()));
			}
		}

		In.DangerMeters = Danger == TNumericLimits<float>::Max() ? 1000.f : Danger / 100.f;
		In.Metrics.Add(MCollapses, static_cast<float>(Collapses));
		In.Metrics.Add(MDebris, Debris);
		In.Metrics.Add(MEnemies, static_cast<float>(Enemies));
		In.Metrics.Add(MWalkers, static_cast<float>(Walkers));
		In.Metrics.Add(MFactions, static_cast<float>(Factions.Num()));
		In.Metrics.Add(MBlasts, static_cast<float>(BlastsInFrame));
		In.Metrics.Add(MDanger, In.DangerMeters);
		In.Metrics.Add(MBlastMeters, NearestBlast == TNumericLimits<float>::Max() ? 1000.f : NearestBlast / 100.f);
		In.Metrics.Add(MCentering, In.Centering);
		// Co-star (R20): the teammate who pays most (frame fill, then survival) near the framed blast, unoccluded.
		const FDCBlastRecord* Subject = OutBlastId != 0 ? GS->FindBlast(OutBlastId) : nullptr;
		const FDCPhotoTuning& PT = UDCTuning::Get().Photo;
		for (ADCCharacter* Mate : TActorRange<ADCCharacter>(World))
		{
			if (!Subject || Mate == Photographer || FVector::Dist(Mate->GetActorLocation(), Subject->Location) > PT.CoStarBlastMeters * 100.f)
			{
				continue;
			}
			FVector2D Head, Feet;
			const FVector Mid = Mate->GetActorLocation();
			if (!ProjectToFrame(View, Mid + FVector(0, 0, 90.f), Head) || !ProjectToFrame(View, Mid - FVector(0, 0, 90.f), Feet) || !InFrame(View, Mid, 1.f))
			{
				continue;
			}
			FHitResult Hit;
			FCollisionQueryParams Q(TEXT("DCCoStar"), false);
			Q.AddIgnoredActor(Photographer);
			Q.AddIgnoredActor(Mate);
			if (World->LineTraceSingleByChannel(Hit, View.Location, Mid, ECC_Visibility, Q))
			{
				continue; // hidden behind something: no co-star
			}
			const float Fill = FMath::Abs(Head.Y - Feet.Y) * 0.5f; // fraction of the frame height
			const bool bDown = Mate->IsDowned();
			if (DCPhotoRules::CoStarMultiplier(Fill, PT, bDown) > DCPhotoRules::CoStarMultiplier(In.CoStarFill, PT, In.bCoStarDowned))
			{
				In.CoStarFill = Fill;
				In.bCoStarDowned = bDown;
				const APlayerState* MatePS = Mate->GetPlayerState();
				In.CoStarName = MatePS ? MatePS->GetPlayerName() : FString();
				In.CoStarPlayerId = MatePS ? MatePS->GetPlayerId() : -1;
			}
		}
		return In;
	}

	void CollectTags(UWorld* World, const FDCShotView& View, float Now, const AActor* Photographer, TArray<FDCLiveTag>& Out)
	{
		Out.Reset();
		const ADCGameState* GS = World ? World->GetGameState<ADCGameState>() : nullptr;
		if (!GS)
		{
			return;
		}
		const float TagRange = UDCTuning::Get().Camera.TagRangeCm;
		for (ADCBuilding* Building : TActorRange<ADCBuilding>(World))
		{
			const float Age = Now - Building->GetCollapseTime();
			if (Building->IsCollapsed() && Age >= 0.f && Age < CollapseAirborneSeconds)
			{
				const FVector P = Building->GetActorLocation() + FVector(0, 0, Building->GetHeight() * 0.6f);
				if (InFrame(View, P, 1.f))
				{
					Out.Add({P, TEXT("DEMOLITION"), 1.f - Age / CollapseAirborneSeconds});
				}
			}
		}
		for (ADCOrbitalStrike* Strike : TActorRange<ADCOrbitalStrike>(World))
		{
			const float Left = Strike->GetDetonateTime() - Now;
			// The tag rides the laser column at the height nearest the view ray, so it sits on the beam in frame
			// (a fixed 9 m tag fell off the bottom of a frame aimed up a tower).
			const FVector F = View.Rotation.Vector();
			const FVector W = Strike->GetActorLocation() - View.Location;
			const float Den = 1.f - F.Z * F.Z;
			const float Up = Den > 0.02f ? (F.Z * FVector::DotProduct(F, W) - W.Z) / Den : 900.f;
			const FVector P = Strike->GetActorLocation() + FVector(0, 0, FMath::Clamp(Up, 300.f, 8000.f));
			if (Left > -1.f && InFrame(View, P, 1.f))
			{
				Out.Add({P, Left > 0.f ? FName(TEXT("DRAMA")) : FName(TEXT("PEAK")), FMath::Clamp(1.f - Left / 3.f, 0.f, 1.f)});
			}
		}
		for (ADCEnemy* Enemy : TActorRange<ADCEnemy>(World))
		{
			const FVector P = Enemy->GetActorLocation() + FVector(0, 0, 150.f);
			if (Enemy->IsAlive() && FVector::Dist(View.Location, P) < TagRange && InFrame(View, P, 1.f))
			{
				// Tag by enemy state, first match wins (a table, not a chain).
				using FTagRule = TPair<bool (*)(const ADCEnemy&, float), const TCHAR*>;
				static const FTagRule Rules[] = {
					{[](const ADCEnemy& En, float T) { return En.IsStunned(T); }, TEXT("STUNNED")},
					{[](const ADCEnemy& En, float) { return En.IsWalker(); }, TEXT("WAR MACHINE")},
					{[](const ADCEnemy&, float) { return true; }, TEXT("BRUTALITY")},
				};
				const FTagRule* Rule = Algo::FindByPredicate(Rules, [Enemy, Now](const FTagRule& R) { return R.Key(*Enemy, Now); });
				Out.Add({P, FName(Rule->Value), 0.5f});
			}
		}
	}
}
