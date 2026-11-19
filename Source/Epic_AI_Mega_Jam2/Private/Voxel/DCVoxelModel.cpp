#include "Voxel/DCVoxelModel.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	constexpr int32 MaxModelEdge = 512;
}

bool FDCVoxelModel::Parse(const FString& Text, FDCVoxelModel& Out, FString& Error)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	if (Lines.Num() < 2 || !Lines[0].StartsWith(TEXT("dcvox 1")))
	{
		Error = TEXT("not a dcvox 1 file");
		return false;
	}
	Out = FDCVoxelModel();
	bool bRle = false;
	int64 Filled = 0;
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		TArray<FString> T;
		Lines[i].ParseIntoArrayWS(T);
		if (T.Num() == 0)
		{
			continue;
		}
		if (!bRle)
		{
			// Header lines: a key and its values (a lookup by key, not a ladder of modes).
			using FHeaderFn = bool (*)(const TArray<FString>&, FDCVoxelModel&);
			static const TMap<FString, FHeaderFn> Header = {
				{TEXT("name"), [](const TArray<FString>& A, FDCVoxelModel& M) { M.Name = FName(A.Num() > 1 ? *A[1] : TEXT("")); return A.Num() > 1; }},
				{TEXT("grid_cm"), [](const TArray<FString>& A, FDCVoxelModel& M) { M.GridCm = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0; return M.GridCm >= 1 && M.GridCm <= 100; }},
				{TEXT("size"), [](const TArray<FString>& A, FDCVoxelModel& M)
					{
						if (A.Num() < 4) { return false; }
						M.Size = FIntVector(FCString::Atoi(*A[1]), FCString::Atoi(*A[2]), FCString::Atoi(*A[3]));
						return M.Size.GetMin() > 0 && M.Size.GetMax() <= MaxModelEdge;
					}},
				{TEXT("pivot"), [](const TArray<FString>& A, FDCVoxelModel& M)
					{
						if (A.Num() < 4) { return false; }
						M.Pivot = FIntVector(FCString::Atoi(*A[1]), FCString::Atoi(*A[2]), FCString::Atoi(*A[3]));
						return true;
					}},
				{TEXT("slots"), [](const TArray<FString>& A, FDCVoxelModel& M)
					{
						for (int32 k = 1; k < A.Num(); ++k) { M.Slots.Add(FName(*A[k])); }
						return M.Slots.Num() < 255;
					}},
				{TEXT("solid"), [](const TArray<FString>&, FDCVoxelModel&) { return true; }},
				// R20: part Role x0 y0 z0 x1 y1 z1 jx jy jz (an animation segment; voxel-forge `part`).
				{TEXT("part"), [](const TArray<FString>& A, FDCVoxelModel& M)
					{
						if (A.Num() != 11 || M.Parts.Num() >= 64) { return false; }
						FDCVoxelPart P;
						P.Role = FName(*A[1]);
						P.Min = FIntVector(FCString::Atoi(*A[2]), FCString::Atoi(*A[3]), FCString::Atoi(*A[4]));
						P.Max = FIntVector(FCString::Atoi(*A[5]), FCString::Atoi(*A[6]), FCString::Atoi(*A[7]));
						P.Joint = FVector(FCString::Atof(*A[8]), FCString::Atof(*A[9]), FCString::Atof(*A[10]));
						M.Parts.Add(P);
						return P.Joint.GetAbsMax() <= MaxModelEdge;
					}},
			};
			if (T[0] == TEXT("rle"))
			{
				if (Out.Size.GetMin() <= 0)
				{
					Error = TEXT("rle before size");
					return false;
				}
				Out.Cells.SetNumZeroed(Out.Size.X * Out.Size.Y * Out.Size.Z);
				bRle = true;
				continue;
			}
			const FHeaderFn* Fn = Header.Find(T[0]);
			if (!Fn || !(*Fn)(T, Out))
			{
				Error = FString::Printf(TEXT("bad header line %d: %s"), i + 1, *Lines[i]);
				return false;
			}
			continue;
		}
		for (const FString& Tok : T)
		{
			FString Count, Slot;
			if (!Tok.Split(TEXT("*"), &Count, &Slot))
			{
				Error = FString::Printf(TEXT("bad run '%s'"), *Tok);
				return false;
			}
			const int64 N = FCString::Atoi64(*Count);
			const int32 S = FCString::Atoi(*Slot);
			if (N <= 0 || S < 0 || S > Out.Slots.Num() || Filled + N > Out.Cells.Num())
			{
				Error = FString::Printf(TEXT("run '%s' out of range"), *Tok);
				return false;
			}
			if (S != 0)
			{
				FMemory::Memset(Out.Cells.GetData() + Filled, static_cast<uint8>(S), N);
			}
			Filled += N;
		}
	}
	if (!bRle || Filled != Out.Cells.Num())
	{
		Error = FString::Printf(TEXT("volume has %lld of %d voxels"), Filled, Out.Cells.Num());
		return false;
	}
	if (Out.SegmentRoles().Num() > MaxSegments)
	{
		Error = FString::Printf(TEXT("%d part roles, at most %d"), Out.SegmentRoles().Num(), MaxSegments);
		return false;
	}
	return true;
}

TArray<FName> FDCVoxelModel::SegmentRoles() const
{
	TArray<FName> Roles;
	for (const FDCVoxelPart& P : Parts)
	{
		Roles.AddUnique(P.Role);
	}
	return Roles;
}

int32 FDCVoxelModel::PartOf(const FIntVector& C) const
{
	for (int32 i = Parts.Num() - 1; i >= 0; --i)
	{
		const FDCVoxelPart& P = Parts[i];
		if (C.X >= P.Min.X && C.Y >= P.Min.Y && C.Z >= P.Min.Z && C.X < P.Max.X && C.Y < P.Max.Y && C.Z < P.Max.Z)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void FDCVoxelModel::BuildUnitMesh(const TArray<FName>& PaletteNames, const TArray<FColor>& PaletteColors, FDCVoxelUnitMesh& Out) const
{
	Out = FDCVoxelUnitMesh();
	Out.Roles = SegmentRoles();
	TArray<uint8> SlotToId;
	SlotToId.Add(0);
	for (const FName& Slot : Slots)
	{
		const int32 Index = PaletteNames.IndexOfByKey(Slot);
		SlotToId.Add(static_cast<uint8>(Index == INDEX_NONE ? 1 : Index + 1));
	}
	// Bucket voxels by owning part (bucket 0 = root body, bucket p + 1 = part p): parts sharing a role (a tripod of
	// legs) share the segment's pose but turn about their own joints.
	const int32 NumBuckets = Parts.Num() + 1;
	TArray<TArray<FIntVector>> Pos;
	TArray<TArray<uint8>> Mats;
	Pos.SetNum(NumBuckets);
	Mats.SetNum(NumBuckets);
	for (int32 Z = 0; Z < Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const uint8 S = Cells[X + Size.X * (Y + Size.Y * Z)];
				if (S != 0)
				{
					const int32 Bucket = PartOf(FIntVector(X, Y, Z)) + 1;
					Pos[Bucket].Add(FIntVector(X, Y, Z));
					Mats[Bucket].Add(SlotToId[S]);
				}
			}
		}
	}
	for (int32 Bucket = 0; Bucket < NumBuckets; ++Bucket)
	{
		if (Pos[Bucket].IsEmpty())
		{
			continue;
		}
		const int32 First = Out.Mesh.Positions.Num();
		DCVoxelMesher::MeshCells(Pos[Bucket], Mats[Bucket], Pivot, PaletteColors, static_cast<float>(GridCm), Out.Mesh);
		const int32 Seg = Bucket == 0 ? 0 : Out.Roles.IndexOfByKey(Parts[Bucket - 1].Role) + 1;
		const FVector JointCm = Bucket == 0 ? FVector::ZeroVector : (Parts[Bucket - 1].Joint - FVector(Pivot)) * GridCm;
		for (int32 v = First; v < Out.Mesh.Positions.Num(); ++v)
		{
			Out.UV1.Add(FVector2D(Seg, JointCm.X));
			Out.UV2.Add(FVector2D(JointCm.Y, JointCm.Z));
		}
	}
}

void FDCVoxelModel::BuildMesh(const TArray<FName>& PaletteNames, const TArray<FColor>& PaletteColors, FDCVoxelMeshData& Out) const
{
	// Slot -> game palette id (palette index + 1), by name.
	TArray<uint8> SlotToId;
	SlotToId.Add(0);
	for (const FName& Slot : Slots)
	{
		const int32 Index = PaletteNames.IndexOfByKey(Slot);
		SlotToId.Add(static_cast<uint8>(Index == INDEX_NONE ? 1 : Index + 1));
	}
	TArray<FIntVector> Pos;
	TArray<uint8> Mats;
	for (int32 Z = 0; Z < Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const uint8 S = Cells[X + Size.X * (Y + Size.Y * Z)];
				if (S != 0)
				{
					Pos.Add(FIntVector(X, Y, Z));
					Mats.Add(SlotToId[S]);
				}
			}
		}
	}
	DCVoxelMesher::MeshCells(Pos, Mats, Pivot, PaletteColors, static_cast<float>(GridCm), Out);
}

namespace DCVoxelModels
{
	const FDCVoxelModel* Parsed(FName Model)
	{
		check(IsInGameThread());
		// Heap entries: the returned pointer stays valid when the map grows (sector crafts read models off-thread).
		static TMap<FName, TUniquePtr<FDCVoxelModel>> Cache;
		if (Model.IsNone())
		{
			return nullptr;
		}
		if (const TUniquePtr<FDCVoxelModel>* Found = Cache.Find(Model))
		{
			return Found->Get();
		}
		TUniquePtr<FDCVoxelModel>& Slot = Cache.Add(Model);
		const FString Path = FPaths::ProjectContentDir() / TEXT("DC/Voxel") / (Model.ToString() + TEXT(".dcvox"));
		FString Text;
		FDCVoxelModel M;
		FString Error;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FDCVoxelModel::Parse(Text, M, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("DCVOXEL model %s not loaded (%s): %s"), *Model.ToString(), *Path, *Error);
			return nullptr;
		}
		Slot = MakeUnique<FDCVoxelModel>(MoveTemp(M));
		return Slot.Get();
	}

	const FDCVoxelUnitMesh* UnitMesh(FName Model)
	{
		check(IsInGameThread());
		static TMap<FName, TOptional<FDCVoxelUnitMesh>> Cache;
		if (const TOptional<FDCVoxelUnitMesh>* Found = Cache.Find(Model))
		{
			return Found->IsSet() ? &Found->GetValue() : nullptr;
		}
		TOptional<FDCVoxelUnitMesh>& Slot = Cache.Add(Model);
		const FDCVoxelModel* M = Parsed(Model);
		if (!M)
		{
			return nullptr;
		}
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		TArray<FName> Names;
		for (const FDCVoxelMaterial& P : V.Palette)
		{
			Names.Add(P.Id);
		}
		Slot.Emplace();
		M->BuildUnitMesh(Names, V.MeshColors(), Slot.GetValue());
		UE_LOG(LogTemp, Display, TEXT("DCVOXEL unit model %s: %d segments, %d triangles"), *Model.ToString(), Slot->Roles.Num() + 1, Slot->Mesh.Indices.Num() / 3);
		return &Slot.GetValue();
	}

	const FDCVoxelMeshData* Mesh(FName Model)
	{
		check(IsInGameThread());
		static TMap<FName, TOptional<FDCVoxelMeshData>> Cache;
		if (const TOptional<FDCVoxelMeshData>* Found = Cache.Find(Model))
		{
			return Found->IsSet() ? &Found->GetValue() : nullptr;
		}
		TOptional<FDCVoxelMeshData>& Slot = Cache.Add(Model);
		const FDCVoxelModel* M = Parsed(Model);
		if (!M)
		{
			return nullptr;
		}
		const UDCVoxelSettings& V = UDCVoxelSettings::Get();
		TArray<FName> Names;
		for (const FDCVoxelMaterial& P : V.Palette)
		{
			Names.Add(P.Id);
		}
		Slot.Emplace();
		M->BuildMesh(Names, V.MeshColors(), Slot.GetValue());
		UE_LOG(LogTemp, Display, TEXT("DCVOXEL model %s: %dx%dx%d @ %d cm, %d triangles"), *Model.ToString(), M->Size.X, M->Size.Y, M->Size.Z, M->GridCm, Slot->Indices.Num() / 3);
		return &Slot.GetValue();
	}
}
