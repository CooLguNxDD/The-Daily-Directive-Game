// R8 player weapons: the gun slot holds every +Weapons row of Config/DefaultGame.ini. Each weapon keeps its own
// magazine and reserve (owner-local and a host copy for validation); switching stashes the one in hand and loads the
// next. The weapon index replicates so every machine shows the weapon's forge model and tracer.
#include "DCCharacter.h"
#include "DCAssets.h"
#include "DCAudio.h"
#include "DCTuning.h"
#include "Voxel/DCVoxelModel.h"
#include "Voxel/DCVoxelSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

const FDCWeaponDef& ADCCharacter::ActiveWeapon() const
{
	return UDCTuning::Get().Weapon(WeaponIndex);
}

void ADCCharacter::InitWeapons()
{
	const UDCTuning& T = UDCTuning::Get();
	WeaponStash.SetNum(T.NumWeapons());
	HostWeaponStash.SetNum(T.NumWeapons());
	for (int32 w = 0; w < T.NumWeapons(); ++w)
	{
		const FDCRifleTuning& Stats = T.Weapon(w).Stats;
		WeaponStash[w] = HostWeaponStash[w] = FIntPoint(Stats.MagazineSize, Stats.StartReserve);
	}
	WeaponIndex = static_cast<uint8>(FMath::Clamp<int32>(WeaponIndex, 0, T.NumWeapons() - 1));
	HostWeapon = WeaponIndex;
	Ammo = HostAmmo = WeaponStash[WeaponIndex].X;
	Reserve = HostReserve = WeaponStash[WeaponIndex].Y;
	ApplyWeaponModel();
}

void ADCCharacter::NextWeapon()
{
	SelectWeapon((WeaponIndex + 1) % UDCTuning::Get().NumWeapons());
}

void ADCCharacter::SelectWeapon(int32 Index)
{
	const UDCTuning& T = UDCTuning::Get();
	if (bDowned || Index == WeaponIndex || !WeaponStash.IsValidIndex(Index) || !WeaponStash.IsValidIndex(WeaponIndex))
	{
		return;
	}
	WeaponStash[WeaponIndex] = FIntPoint(Ammo, Reserve);
	WeaponIndex = static_cast<uint8>(Index);
	Ammo = WeaponStash[Index].X;
	Reserve = WeaponStash[Index].Y;
	ReloadEndTime = -1.f; // a switch cancels a reload, like a slot change
	DrawEndTime = Now() + T.Camera.DrawSeconds;
	DCAudio::PlayAt(this, TEXT("Draw"), GetActorLocation());
	ApplyWeaponModel();
	Toast(T.Weapon(Index).DisplayName.ToString());
	ServerSetWeapon(WeaponIndex);
}

void ADCCharacter::ServerSetWeapon_Implementation(uint8 Index)
{
	if (bDowned || Index == HostWeapon || !HostWeaponStash.IsValidIndex(Index) || !HostWeaponStash.IsValidIndex(HostWeapon))
	{
		return; // untrusted intent: an index outside the table is ignored
	}
	HostWeaponStash[HostWeapon] = FIntPoint(HostAmmo, HostReserve);
	HostWeapon = Index;
	HostAmmo = HostWeaponStash[Index].X;
	HostReserve = HostWeaponStash[Index].Y;
	HostNextFireTime = Now() + UDCTuning::Get().Camera.DrawSeconds * 0.8f;
	WeaponIndex = Index;
	ApplyWeaponModel();
}

void ADCCharacter::OnRep_Weapon()
{
	ApplyWeaponModel();
}

void ADCCharacter::ApplyWeaponModel()
{
	const FDCWeaponDef& W = ActiveWeapon();
	if (UMaterialInstanceDynamic* TracerLook = Tracer ? Cast<UMaterialInstanceDynamic>(Tracer->GetMaterial(0)) : nullptr)
	{
		DCAssets::SetFlat(TracerLook, W.TracerColor, W.TracerGlow);
	}
	const FDCVoxelMeshData* Gun = DCVoxelModels::Mesh(W.Model);
	if (!RifleMesh || !Gun)
	{
		return; // no model on disk: the prop cube stays
	}
	RifleMesh->SetStaticMesh(nullptr);
	if (!GunVox)
	{
		GunVox = NewObject<UProceduralMeshComponent>(this, TEXT("RifleVox"));
		GunVox->SetupAttachment(RifleMesh);
		GunVox->SetUsingAbsoluteScale(true); // the prop squashes its cube by scale; the model keeps its own proportions
		GunVox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GunVox->RegisterComponent();
	}
	GunVox->ClearAllMeshSections();
	GunVox->CreateMeshSection(0, Gun->Positions, Gun->Indices, Gun->Normals, Gun->UVs, Gun->Colors, {}, false);
	GunVox->SetMaterial(0, Cast<UMaterialInterface>(UDCVoxelSettings::Get().VoxelMaterial.TryLoad()));
	GunVox->SetRelativeLocationAndRotation(W.ModelLocation, W.ModelRotation);
	GunVox->SetWorldScale3D(FVector(FMath::Max(W.ModelScale, 0.05f)));
}
