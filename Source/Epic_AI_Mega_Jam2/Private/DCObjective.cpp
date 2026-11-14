#include "DCObjective.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCTuning.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"

ADCObjective::ADCObjective()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(RootComponent);
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);
	Beacon->SetVisibility(false);
	BeaconLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(RootComponent);
	BeaconLight->SetCastShadows(false);
	BeaconLight->SetVisibility(false);
	PackageLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PackageLight"));
	PackageLight->SetupAttachment(RootComponent);
	PackageLight->SetCastShadows(false);
	PackageLight->SetVisibility(false);
}

void ADCObjective::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADCObjective, DefId);
	DOREPLIFETIME(ADCObjective, Stage);
	DOREPLIFETIME(ADCObjective, PackageAt);
	DOREPLIFETIME(ADCObjective, Carrier);
	DOREPLIFETIME(ADCObjective, TargetAt);
	DOREPLIFETIME(ADCObjective, TargetDown);
	DOREPLIFETIME(ADCObjective, ExtractAt);
}

const FDCObjectiveDef* ADCObjective::GetDef() const
{
	return UDCTuning::Get().Objectives.FindByPredicate([this](const FDCObjectiveDef& D) { return D.Id == DefId; });
}

ADCObjective* ADCObjective::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (ADCObjective* O : TActorRange<ADCObjective>(World))
	{
		return O;
	}
	return nullptr;
}

int32 ADCObjective::TargetsLeft() const
{
	int32 Left = 0;
	for (int32 i = 0; i < TargetAt.Num(); ++i)
	{
		Left += TargetDown.IsValidIndex(i) && TargetDown[i] ? 0 : 1;
	}
	return Left;
}

bool ADCObjective::Waypoint(const FVector& From, FVector& Out) const
{
	const FDCObjectiveDef* Def = GetDef();
	if (!Def || Stage == EDCObjectiveStage::None || Stage == EDCObjectiveStage::Done || Stage == EDCObjectiveStage::Failed)
	{
		return false;
	}
	if (Stage == EDCObjectiveStage::Extract && (Def->Kind != TEXT("Retrieve") || Carrier))
	{
		Out = ExtractAt;
		return true;
	}
	if (Def->Kind == TEXT("Retrieve"))
	{
		Out = PackageAt;
		return true;
	}
	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; i < TargetAt.Num(); ++i)
	{
		const float D = TargetDown.IsValidIndex(i) && TargetDown[i] ? Best : FVector::DistSquared2D(TargetAt[i], From);
		Out = D < Best ? FVector(TargetAt[i]) : Out;
		Best = FMath::Min(Best, D);
	}
	return Best < TNumericLimits<float>::Max();
}

void ADCObjective::BuildVisuals()
{
	const FDCObjectiveLook& L = UDCTuning::Get().ObjectiveLook;
	UMaterialInterface* Glow = DCAssets::MakeFlatUnlit(this, L.Color, L.BeaconGlow);
	Beacon->SetStaticMesh(DCAssets::Cylinder());
	Beacon->SetMaterial(0, Glow);
	Beacon->SetRelativeScale3D(FVector(L.BeaconWidthCm / 100.f, L.BeaconWidthCm / 100.f, L.BeaconHeightCm / 100.f));
	BeaconLight->SetLightColor(L.Color);
	BeaconLight->SetIntensity(L.BeaconCandela);
	BeaconLight->SetAttenuationRadius(L.BeaconLightRadiusCm);
	PackageLight->SetLightColor(L.Color);
	PackageLight->SetIntensity(L.PackageCandela);
	PackageLight->SetAttenuationRadius(L.PackageLightRadiusCm);
	if (const FDCVoxelMeshData* Mesh = DCVoxelModels::Mesh(L.PackageModel))
	{
		Package = NewObject<UProceduralMeshComponent>(this, TEXT("Package"));
		Package->SetupAttachment(RootComponent);
		Package->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Package->RegisterComponent();
		Package->CreateMeshSection(0, Mesh->Positions, Mesh->Indices, Mesh->Normals, Mesh->UVs, Mesh->Colors, {}, false);
		Package->SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
		Package->SetRelativeScale3D(FVector(L.PackageScale));
		Package->SetVisibility(false);
	}
	UMaterialInterface* MarkerGlow = DCAssets::MakeFlatUnlit(this, L.Color, L.MarkerGlow);
	for (int32 i = 0; i < 8; ++i)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetupAttachment(RootComponent);
		M->SetStaticMesh(DCAssets::Cylinder());
		M->SetMaterial(0, MarkerGlow);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetRelativeScale3D(FVector(L.MarkerWidthCm / 100.f, L.MarkerWidthCm / 100.f, L.MarkerHeightCm / 100.f));
		M->SetVisibility(false);
		M->RegisterComponent();
		Markers.Add(M);
	}
	bBuilt = true;
}

void ADCObjective::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		BuildVisuals();
	}
	const FDCObjectiveDef* Def = GetDef();
	const FDCObjectiveLook& L = UDCTuning::Get().ObjectiveLook;
	const bool bLive = Def && (Stage == EDCObjectiveStage::Active || Stage == EDCObjectiveStage::Extract);
	// The beacon (engine cylinder: 100 cm tall, pivot at its centre).
	const bool bBeacon = bLive && Stage == EDCObjectiveStage::Extract;
	Beacon->SetVisibility(bBeacon);
	BeaconLight->SetVisibility(bBeacon);
	if (bBeacon)
	{
		Beacon->SetWorldLocation(FVector(ExtractAt) + FVector(0, 0, L.BeaconHeightCm * 0.5f));
		BeaconLight->SetWorldLocation(FVector(ExtractAt) + FVector(0, 0, 250.f));
	}
	// The package: on the street, or riding its carrier.
	const bool bPackage = bLive && Def->Kind == TEXT("Retrieve");
	const APawn* Bearer = Carrier ? Carrier->GetPawn() : nullptr;
	const FVector PackageWorld = Bearer ? Bearer->GetActorTransform().TransformPosition(L.CarryOffset) : FVector(PackageAt);
	if (Package)
	{
		Package->SetVisibility(bPackage);
		Package->SetWorldLocation(PackageWorld);
		Package->SetWorldRotation(Bearer ? Bearer->GetActorRotation() : FRotator(0.f, GetWorld()->GetTimeSeconds() * 45.f, 0.f));
	}
	PackageLight->SetVisibility(bPackage);
	PackageLight->SetWorldLocation(PackageWorld + FVector(0, 0, 120.f));
	// Marker pillars above every standing target.
	for (int32 i = 0; i < Markers.Num(); ++i)
	{
		const bool bShow = bLive && Stage == EDCObjectiveStage::Active && TargetAt.IsValidIndex(i) && !(TargetDown.IsValidIndex(i) && TargetDown[i]);
		Markers[i]->SetVisibility(bShow);
		if (bShow)
		{
			Markers[i]->SetWorldLocation(FVector(TargetAt[i]) + FVector(0, 0, L.MarkerLiftCm + L.MarkerHeightCm * 0.5f));
		}
	}
}
