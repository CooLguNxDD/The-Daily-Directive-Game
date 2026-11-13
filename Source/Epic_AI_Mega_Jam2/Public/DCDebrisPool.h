// Local-only pool of simulated debris blocks (Chaos rigid bodies). Collapses borrow pieces, pieces return when
// their life ends. Never replicated: every client simulates its own debris; scoring never reads it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCDebrisPool.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

USTRUCT()
struct FDCDebrisSpawn
{
	GENERATED_BODY()
	FTransform Transform;
	FLinearColor Color = FLinearColor::White;
	float Glow = 0.f;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocityDeg = FVector::ZeroVector;
	float LifeSeconds = 6.f;
};

UCLASS(NotPlaceable)
class EPIC_AI_MEGA_JAM2_API ADCDebrisPool : public AActor
{
	GENERATED_BODY()

public:
	ADCDebrisPool();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Returns false when the pool is exhausted (the caller drops the piece). */
	bool Spawn(const FDCDebrisSpawn& Spec);

	int32 ActiveCount() const { return Active.Num(); }

private:
	struct FPiece
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		float DieTime = 0.f;
		FVector BaseScale = FVector::OneVector;
	};

	UMaterialInstanceDynamic* MaterialFor(const FLinearColor& Color, float Glow);
	void Release(int32 ActiveIndex);

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Free;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AllPieces;
	TArray<FPiece> Active;
	UPROPERTY() TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> Materials;
};
