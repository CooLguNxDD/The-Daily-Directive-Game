#include "Voxel/DCVoxelMesher.h"

#include "Voxel/DCVoxelWorld.h"

namespace
{
	constexpr int32 MeshS = FDCVoxelChunk::Size;
	constexpr int32 MeshP = MeshS + 2;

	struct FPaddedView
	{
		const uint8* V;
		uint8 At(const int32 C[3]) const { return V[(C[2] * MeshP + C[1]) * MeshP + C[0]]; }
		bool Solid(const int32 C[3]) const { return At(C) != 0; }
	};

	/** Ambient occlusion of one face corner: 3 = open, 0 = fully occluded (two sides solid). */
	int32 CornerAO(bool bSide1, bool bSide2, bool bCorner)
	{
		return bSide1 && bSide2 ? 0 : 3 - (static_cast<int32>(bSide1) + static_cast<int32>(bSide2) + static_cast<int32>(bCorner));
	}

	void EmitQuad(FDCVoxelMeshData& Out, const FVector (&Corners)[4], const FVector& Normal, const FVector2D (&Uv)[4], const FColor& Base, const int32 (&AO)[4], float AOStrength)
	{
		const int32 First = Out.Positions.Num();
		for (int32 k = 0; k < 4; ++k)
		{
			Out.Positions.Add(Corners[k]);
			Out.Normals.Add(Normal);
			Out.UVs.Add(Uv[k]);
			const float AOShade = 1.f - AOStrength * (3 - AO[k]) / 3.f;
			Out.Colors.Add(FColor(static_cast<uint8>(Base.R * AOShade), static_cast<uint8>(Base.G * AOShade), static_cast<uint8>(Base.B * AOShade), Base.A));
		}
		// Split along the diagonal that keeps occlusion gradients smooth.
		const bool bFlipDiagonal = AO[0] + AO[2] < AO[1] + AO[3];
		const int32 Tri[6] = {0, 1, 2, 0, 2, 3};
		const int32 TriFlip[6] = {1, 2, 3, 1, 3, 0};
		const int32* Order = bFlipDiagonal ? TriFlip : Tri;
		for (int32 t = 0; t < 6; t += 3)
		{
			const FVector& A = Corners[Order[t]];
			const FVector& B = Corners[Order[t + 1]];
			const FVector& C = Corners[Order[t + 2]];
			// Unreal's front face: (B - A) x (C - A) points away from the normal (see the PMC box generator).
			const bool bSwap = FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Normal) > 0.f;
			Out.Indices.Add(First + Order[t]);
			Out.Indices.Add(First + Order[bSwap ? t + 2 : t + 1]);
			Out.Indices.Add(First + Order[bSwap ? t + 1 : t + 2]);
		}
	}
}

namespace DCVoxelMesher
{
	void MeshPadded(const TArray<uint8>& Padded, const FDCVoxelColors& Colors, float VoxelSizeCm, float AOStrength, FDCVoxelMeshData& Out)
	{
		check(Padded.Num() == MeshP * MeshP * MeshP);
		const FPaddedView View{Padded.GetData()};
		TArray<uint32> Mask;
		Mask.SetNumUninitialized(MeshS * MeshS);
		static const FColor Missing(255, 0, 255, 0);

		for (int32 D = 0; D < 3; ++D)
		{
			const int32 U = (D + 1) % 3;
			const int32 V = (D + 2) % 3;
			for (int32 Slice = 0; Slice <= MeshS; ++Slice)
			{
				// Build the face mask between interior layer Slice-1 (A) and Slice (B). Padded index = interior + 1.
				for (int32 J = 0; J < MeshS; ++J)
				{
					for (int32 I = 0; I < MeshS; ++I)
					{
						int32 A[3], B[3];
						A[D] = Slice;     // padded index of interior Slice-1
						B[D] = Slice + 1; // padded index of interior Slice
						A[U] = B[U] = I + 1;
						A[V] = B[V] = J + 1;
						const uint8 Ma = View.At(A);
						const uint8 Mb = View.At(B);
						uint32 Key = 0;
						const bool bFront = Ma != 0 && Mb == 0 && Slice > 0;     // face of A looking +D
						const bool bBack = Mb != 0 && Ma == 0 && Slice < MeshS;      // face of B looking -D
						if (bFront || bBack)
						{
							const int32* Air = bFront ? B : A;
							int32 AO[4];
							static const int32 Su[4] = {-1, 1, 1, -1};
							static const int32 Sv[4] = {-1, -1, 1, 1};
							for (int32 k = 0; k < 4; ++k)
							{
								int32 C1[3] = {Air[0], Air[1], Air[2]};
								int32 C2[3] = {Air[0], Air[1], Air[2]};
								int32 C3[3] = {Air[0], Air[1], Air[2]};
								C1[U] += Su[k];
								C2[V] += Sv[k];
								C3[U] += Su[k];
								C3[V] += Sv[k];
								AO[k] = CornerAO(View.Solid(C1), View.Solid(C2), View.Solid(C3));
							}
							const uint32 Mat = bFront ? Ma : Mb;
							Key = Mat | (static_cast<uint32>(bFront ? 1 : 2) << 8) | (static_cast<uint32>(AO[0] | (AO[1] << 2) | (AO[2] << 4) | (AO[3] << 6)) << 10);
						}
						Mask[I + J * MeshS] = Key;
					}
				}
				// Greedy merge of equal keys into rectangles.
				for (int32 J = 0; J < MeshS; ++J)
				{
					for (int32 I = 0; I < MeshS;)
					{
						const uint32 Key = Mask[I + J * MeshS];
						if (Key == 0)
						{
							++I;
							continue;
						}
						int32 W = 1;
						while (I + W < MeshS && Mask[I + W + J * MeshS] == Key)
						{
							++W;
						}
						int32 H = 1;
						for (; J + H < MeshS; ++H)
						{
							bool bRow = true;
							for (int32 k = 0; k < W && bRow; ++k)
							{
								bRow = Mask[I + k + (J + H) * MeshS] == Key;
							}
							if (!bRow)
							{
								break;
							}
						}
						for (int32 h = 0; h < H; ++h)
						{
							for (int32 k = 0; k < W; ++k)
							{
								Mask[I + k + (J + h) * MeshS] = 0;
							}
						}

						const uint8 Mat = static_cast<uint8>(Key & 0xFF);
						const bool bFront = ((Key >> 8) & 3) == 1;
						const int32 Packed = static_cast<int32>(Key >> 10);
						const int32 AO[4] = {Packed & 3, (Packed >> 2) & 3, (Packed >> 4) & 3, (Packed >> 6) & 3};
						FVector Corners[4];
						FVector2D Uv[4];
						const int32 Cu[4] = {I, I + W, I + W, I};
						const int32 Cv[4] = {J, J, J + H, J + H};
						for (int32 k = 0; k < 4; ++k)
						{
							FVector Pos;
							Pos[D] = Slice * VoxelSizeCm;
							Pos[U] = Cu[k] * VoxelSizeCm;
							Pos[V] = Cv[k] * VoxelSizeCm;
							Corners[k] = Pos;
							Uv[k] = FVector2D(Cu[k], Cv[k]);
						}
						FVector Normal = FVector::ZeroVector;
						Normal[D] = bFront ? 1.f : -1.f;
						EmitQuad(Out, Corners, Normal, Uv, Colors.IsValidIndex(Mat) ? Colors[Mat] : Missing, AO, AOStrength);
						I += W;
					}
				}
			}
		}
	}

	void MeshCells(const TArray<FIntVector>& Cells, const TArray<uint8>& Materials, const FIntVector& Origin, const FDCVoxelColors& Colors, float VoxelSizeCm, FDCVoxelMeshData& Out)
	{
		// Reuse the greedy mesher: drop the cells into a scratch world and mesh its chunks.
		FDCVoxelWorld Scratch;
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			Scratch.Set(Cells[i] - Origin, Materials[i]);
		}
		TArray<FIntVector> Keys;
		Scratch.ForEachChunk([&Keys](const FIntVector& Key, const FDCVoxelChunk&) { Keys.Add(Key); });
		Keys.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
		TArray<uint8> Padded;
		FDCVoxelMeshData Part;
		for (const FIntVector& Key : Keys)
		{
			Scratch.CopyPadded(Key, Padded);
			Part.Reset();
			MeshPadded(Padded, Colors, VoxelSizeCm, 0.3f, Part);
			const FVector Offset = FVector(Key * FDCVoxelChunk::Size) * VoxelSizeCm;
			const int32 Base = Out.Positions.Num();
			for (const FVector& Pos : Part.Positions)
			{
				Out.Positions.Add(Pos + Offset);
			}
			Out.Normals.Append(Part.Normals);
			Out.UVs.Append(Part.UVs);
			Out.Colors.Append(Part.Colors);
			for (int32 Index : Part.Indices)
			{
				Out.Indices.Add(Base + Index);
			}
		}
	}
}
