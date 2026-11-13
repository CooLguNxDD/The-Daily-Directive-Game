#include "DCPhotoProbe.h"

#include "DCBuilding.h"
#include "DCEnemy.h"
#include "DCGameState.h"
#include "DCOrbitalStrike.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const FName MCollapses(TEXT("Collapses"));
	const FName MDebris(TEXT("Debris"));
	const FName MEnemies(TEXT("Enemies"));
	const FName MWalkers(TEXT("Walkers"));
	const FName MBlasts(TEXT("Blasts"));
	const FName MDanger(TEXT("DangerMeters"));
	const FName MBlastMeters(TEXT("BlastMeters"));
	const FName MCentering(TEXT("Centering"));

	constexpr float BlastMemorySeconds = 4.f;
	constexpr float CollapseAirborneSeconds = 3.f;
	constexpr float FireballHeightCm = 600.f;

	float VisibleFraction(UWorld* World, const FVector& From, const TArray<FVector>& Points, const AActor* Ignore)
	{
		if (Points.Num() == 0)
		{
			return 0.f;
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
			FVector2D Ndc;
			if (!ProjectToFrame(View, C, Ndc) || FMath::Abs(Ndc.X) > 1.1f || FMath::Abs(Ndc.Y) > 1.1f)
			{
				continue;
			}
			BlastSamples(B, Samples);
			const float Vis = VisibleFraction(World, View.Location, Samples, Photographer);
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
			if (!bFramed || VisibleFraction(World, View.Location, Samples, Photographer) <= 0.f)
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
		In.Metrics.Add(MBlasts, static_cast<float>(BlastsInFrame));
		In.Metrics.Add(MDanger, In.DangerMeters);
		In.Metrics.Add(MBlastMeters, NearestBlast == TNumericLimits<float>::Max() ? 1000.f : NearestBlast / 100.f);
		In.Metrics.Add(MCentering, In.Centering);
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
			const FVector P = Strike->GetActorLocation() + FVector(0, 0, 900.f);
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
				Out.Add({P, Enemy->IsWalker() ? FName(TEXT("WAR MACHINE")) : FName(TEXT("BRUTALITY")), 0.5f});
			}
		}
	}
}
