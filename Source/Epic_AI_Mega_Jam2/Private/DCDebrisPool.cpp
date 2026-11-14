#include "DCDebrisPool.h"

#include "Components/StaticMeshComponent.h"
#include "DCAssets.h"
#include "DCTuning.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float ShrinkSeconds = 1.f;
}

ADCDebrisPool::ADCDebrisPool()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADCDebrisPool::BeginPlay()
{
	Super::BeginPlay();
	const int32 Size = UDCTuning::Get().City.DebrisPoolSize;
	UStaticMesh* Cube = DCAssets::Cube();
	Free.Reserve(Size);
	for (int32 i = 0; i < Size; ++i)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(true);
		Mesh->SetVisibility(false);
		Mesh->SetupAttachment(RootComponent);
		Mesh->RegisterComponent();
		Free.Add(Mesh);
		AllPieces.Add(Mesh);
	}
}

UMaterialInstanceDynamic* ADCDebrisPool::MaterialFor(const FLinearColor& InColor, float Glow)
{
	// Unlit chips keep their hue but never a paint-bright albedo: under the moon and the blast light a pale chip read as
	// a cream cube against the lens (peak glare). Glowing chips (enemy eyes) keep their colour.
	const float Cap = UDCTuning::Get().Look.DebrisAlbedoCap;
	const float Max = InColor.GetMax();
	const FLinearColor Color = (Glow <= 0.f && Cap > 0.f && Max > Cap) ? InColor * (Cap / Max) : InColor;
	const uint32 Key = HashCombine(GetTypeHash(Color.ToFColor(true)), GetTypeHash(FMath::RoundToInt(Glow * 10.f)));
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Mid = DCAssets::MakeFlat(this, Color, Glow);
	Materials.Add(Key, Mid);
	return Mid;
}

bool ADCDebrisPool::Spawn(const FDCDebrisSpawn& Spec)
{
	if (Free.Num() == 0 && Active.Num() > 0)
	{
		// Recycle the piece closest to dying, so a second collapse inside the debris lifetime still shows.
		int32 Oldest = 0;
		for (int32 i = 1; i < Active.Num(); ++i)
		{
			Oldest = Active[i].DieTime < Active[Oldest].DieTime ? i : Oldest;
		}
		Release(Oldest);
	}
	if (Free.Num() == 0)
	{
		return false;
	}
	UStaticMeshComponent* Mesh = Free.Pop(EAllowShrinking::No);
	Mesh->SetMaterial(0, MaterialFor(Spec.Color, Spec.Glow));
	Mesh->SetWorldTransform(Spec.Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetVisibility(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Spec.LinearVelocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(Spec.AngularVelocityDeg);

	FPiece& Piece = Active.AddDefaulted_GetRef();
	Piece.Mesh = Mesh;
	Piece.DieTime = GetWorld()->GetTimeSeconds() + Spec.LifeSeconds;
	Piece.BaseScale = Spec.Transform.GetScale3D();
	return true;
}

void ADCDebrisPool::Release(int32 ActiveIndex)
{
	UStaticMeshComponent* Mesh = Active[ActiveIndex].Mesh;
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetVisibility(false);
	Free.Add(Mesh);
	Active.RemoveAtSwap(ActiveIndex, EAllowShrinking::No);
}

void ADCDebrisPool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = Active.Num() - 1; i >= 0; --i)
	{
		const float Left = Active[i].DieTime - Now;
		if (Left <= 0.f)
		{
			Release(i);
			continue;
		}
		if (Left < ShrinkSeconds)
		{
			Active[i].Mesh->SetWorldScale3D(Active[i].BaseScale * FMath::Max(Left / ShrinkSeconds, 0.05f));
		}
	}
}
