#include "DCCityPreviewCommandlet.h"

#include "DCVoxelCity.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rules/DCCityPlan.h"
#include "Voxel/DCVoxelSettings.h"

namespace
{
	constexpr float PreviewCmPerPx = 25.f;

	struct FCanvasPx
	{
		int32 W = 0, H = 0;
		float HalfCm = 0.f;
		TArray<FColor> Px;

		FIntPoint ToPx(const FVector& Cm) const
		{
			return FIntPoint(FMath::RoundToInt32((Cm.X + HalfCm) / PreviewCmPerPx), FMath::RoundToInt32((Cm.Y + HalfCm) / PreviewCmPerPx));
		}
		void Rect(const FVector& Centre, float HalfX, float HalfY, const FColor& C)
		{
			const FIntPoint A = ToPx(Centre - FVector(HalfX, HalfY, 0)), B = ToPx(Centre + FVector(HalfX, HalfY, 0));
			for (int32 Y = FMath::Max(A.Y, 0); Y < FMath::Min(B.Y, H); ++Y)
			{
				for (int32 X = FMath::Max(A.X, 0); X < FMath::Min(B.X, W); ++X)
				{
					Px[Y * W + X] = C;
				}
			}
		}
		void Line(const FVector& From, const FVector& To, const FColor& C)
		{
			const int32 Steps = FMath::Max(1, FMath::RoundToInt32(FVector::Dist2D(From, To) / PreviewCmPerPx));
			for (int32 i = 0; i <= Steps; ++i)
			{
				const FIntPoint P = ToPx(FMath::Lerp(From, To, static_cast<float>(i) / Steps));
				for (int32 d = -1; d <= 1; ++d)
				{
					if (P.X + d >= 0 && P.X + d < W && P.Y >= 0 && P.Y < H)
					{
						Px[P.Y * W + P.X + d] = C;
					}
				}
			}
		}
	};
}

bool UDCCityPreviewCommandlet::WritePlanPng(const FDCCityPlan& Plan, const FString& Path)
{
	FCanvasPx C;
	C.HalfCm = Plan.HalfExtent + 1000.f;
	C.W = C.H = FMath::RoundToInt32(2.f * C.HalfCm / PreviewCmPerPx);
	C.Px.Init(FColor(30, 28, 27), C.W * C.H); // street ink
	const float Lot = Plan.LotSizeCm > 0.f ? Plan.LotSizeCm : 2600.f;
	const FColor Paving(150, 138, 112), Plaza(242, 232, 208), Rubble(92, 70, 52), Prop(255, 198, 26), Sight(216, 38, 28);
	for (const FDCLotPlan& L : Plan.Buildings)
	{
		C.Rect(L.Location, Lot * 0.5f, Lot * 0.5f, Paving);
	}
	for (const FVector& P : Plan.Plazas)
	{
		C.Rect(P, Lot * 0.5f, Lot * 0.5f, Plaza);
	}
	for (const FVector& P : Plan.RubbleZones)
	{
		C.Rect(P, Lot * 0.5f, Lot * 0.5f, Rubble);
	}
	for (int32 i = 0; i < Plan.Buildings.Num(); ++i)
	{
		// Footprint, darker the taller: a tower plan reads like the city seen from orbit.
		const FDCLotPlan& L = Plan.Buildings[i];
		const uint8 Shade = static_cast<uint8>(FMath::Clamp(110 - L.Spec.Floors * 4, 10, 110));
		const float Hx = L.Spec.SizeX * L.Spec.BlockSize * 0.5f, Hy = L.Spec.SizeY * L.Spec.BlockSize * 0.5f;
		if (i == Plan.Landmark)
		{
			C.Rect(L.Location, Hx + 120.f, Hy + 120.f, Prop);
		}
		C.Rect(L.Location, Hx, Hy, FColor(Shade, Shade, static_cast<uint8>(Shade + 6)));
	}
	for (const FDCPropPlan& P : Plan.Props)
	{
		C.Rect(P.Location, 60.f, 60.f, Prop);
	}
	C.Line(Plan.SightFrom, Plan.SightTo, Sight);
	C.Rect(Plan.SightFrom, 150.f, 150.f, Sight);
	for (FColor& Px : C.Px)
	{
		Px.A = 255;
	}
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetRaw(C.Px.GetData(), C.Px.Num() * sizeof(FColor), C.W, C.H, ERGBFormat::BGRA, 8))
	{
		return false;
	}
	const TArray64<uint8>& Bytes = Png->GetCompressed();
	return FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.GetData(), static_cast<int32>(Bytes.Num())), *Path);
}

int32 UDCCityPreviewCommandlet::Main(const FString& Params)
{
	int32 Seed = 4242, Sectors = 3;
	FString Out = FPaths::ProjectSavedDir() / TEXT("CityPreview");
	FParse::Value(*Params, TEXT("seed="), Seed);
	FParse::Value(*Params, TEXT("sectors="), Sectors);
	FParse::Value(*Params, TEXT("out="), Out);
	int32 Failed = 0;
	for (int32 S = 0; S < Sectors; ++S)
	{
		const FDCCityPlan Plan = ADCVoxelCity::PlanSector(Seed, S);
		const FString Path = Out / FString::Printf(TEXT("plan_seed%d_sector%d.png"), Seed, S);
		const bool bOk = WritePlanPng(Plan, Path);
		Failed += bOk ? 0 : 1;
		UE_LOG(LogTemp, Display, TEXT("DCPREVIEW sector=%d buildings=%d plazas=%d rubble=%d props=%d landmark=%d -> %s %s"), S, Plan.Buildings.Num(), Plan.Plazas.Num(),
			Plan.RubbleZones.Num(), Plan.Props.Num(), Plan.Landmark, *Path, bOk ? TEXT("ok") : TEXT("FAILED"));
	}
	// -render: the rendered sector too (R16). A commandlet has no renderer, so the tool starts the game for each sector
	// with -dccityshot (it builds that sector's voxel city, writes aerial frames into Out and quits) and waits for it.
	if (FParse::Param(*Params, TEXT("render")))
	{
		const FString Exe = FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::BaseDir()) / TEXT("UnrealEditor.exe"));
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		const FString OutFull = FPaths::ConvertRelativePathToFull(Out);
		for (int32 S = 0; S < Sectors; ++S)
		{
			const FString Args = FString::Printf(TEXT("\"%s\" -game -windowed -resx=1600 -resy=900 -dcdemo -dcseed=%d -dcstartsector=%d -dccityshot=\"%s\" -unattended -nosplash -nosound"),
				*Project, Seed, S, *OutFull);
			FProcHandle Proc = FPlatformProcess::CreateProc(*Exe, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
			if (!Proc.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("DCPREVIEW render sector=%d could not start %s"), S, *Exe);
				++Failed;
				continue;
			}
			const double GiveUp = FPlatformTime::Seconds() + 300.0;
			while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() < GiveUp)
			{
				FPlatformProcess::Sleep(0.5f);
			}
			if (FPlatformProcess::IsProcRunning(Proc))
			{
				FPlatformProcess::TerminateProc(Proc, true);
				UE_LOG(LogTemp, Warning, TEXT("DCPREVIEW render sector=%d timed out after 300 s"), S);
			}
			FPlatformProcess::CloseProc(Proc);
			TArray<FString> Shots;
			IFileManager::Get().FindFiles(Shots, *(OutFull / FString::Printf(TEXT("cityshot_sector%d_*.png"), S)), true, false);
			UE_LOG(LogTemp, Display, TEXT("DCPREVIEW render sector=%d frames=%d -> %s"), S, Shots.Num(), *OutFull);
			Failed += Shots.Num() > 0 ? 0 : 1;
		}
	}
	return Failed == 0 ? 0 : 1;
}
