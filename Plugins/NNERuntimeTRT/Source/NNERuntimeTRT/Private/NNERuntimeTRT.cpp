#include "NNERuntimeTRT.h"

#include "Containers/ArrayView.h"
#include "Containers/StringConv.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Memory/SharedBuffer.h"
#include "Misc/FileHelper.h"
#include "NNERuntimeTRTLog.h"
#include "NNERuntimeTRTModel.h"
#include "Templates/UniquePtr.h"

// TRT
PRAGMA_DISABLE_DEPRECATION_WARNINGS

#include <NvInfer.h>
#include <NvOnnxConfig.h>
#include <NvOnnxParser.h>

#include "HAL/Platform.h"
#include "Interfaces/ITargetPlatform.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeTRTModelData.h"
#include "NNERuntimeTRTSettings.h"
#include "RHIGlobals.h"
#include "RHIStrings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/GarbageCollection.h"

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#endif // PLATFORM_WINDOWS

#ifdef WITH_NNE_RUNTIME_TRT

// TODO: only need one of these, move it somewhere common
class Logger : public nvinfer1::ILogger
{
public:
	void log(Severity severity, const char* msg) noexcept override
	{
		if (severity <= Severity::kWARNING)
		{
			UE_LOG(LogNNERuntimeTRT, Warning, TEXT("%hs"), msg);
		}
	}
} TRTBuildLogger;

FGuid UNNERuntimeTRT::GUID = FGuid((int32)'T', (int32)'R', (int32)'T', (int32)'R'); // Guid for this plugin, not sure this is ideal
int32 UNNERuntimeTRT::Version = 0x00000002; // Increment each time UNNERuntimeTRTModelData serialization changes

// Engine build precision, selected by the game (set the CVar before the engine is built):
// 0 = FP32, 1 = FP16 (default), 2 = INT8. The built engine is cached per precision (on disk + DDC), so each
// precision builds once; changing precision takes effect on the next engine build (e.g. after an editor restart).
static int32 GNNERuntimeTRTPrecision = 0;
static FAutoConsoleVariableRef CVarNNERuntimeTRTPrecision(
	TEXT("NNE.TRT.Precision"),
	GNNERuntimeTRTPrecision,
	TEXT("TensorRT-RTX engine build precision: 0=FP32, 1=FP16, 2=INT8 (INT8 needs calibration; falls back to FP16). Applies on engine (re)build."),
	ECVF_Default);

static int32 GetTRTBuildPrecision() { return FMath::Clamp(GNNERuntimeTRTPrecision, 0, 2); }

namespace UE::NNERuntimeTRT::Private::Details
{

FString GetIntermediateModelDirPath(const FString& PlatformName, const FString& RuntimeName, const FString& ModelName)
{
	return FPaths::Combine("Intermediate", "Build", (PlatformName.Equals("Windows") ? TEXT("Win64") : PlatformName), RuntimeName, ModelName);
}

template <class CanCreateModelStatus>
CanCreateModelStatus CanCreateModel(const TObjectPtr<UNNEModelData> ModelData, const FString& RuntimeName)
{
	check(ModelData != nullptr);

	const TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(RuntimeName);
	if (!SharedData.IsValid())
	{
		return CanCreateModelStatus::Fail;
	}

	if (!UE::NNERuntimeTRT::Private::FModelData::IsSameGuidAndVersion(SharedData->GetView(), UNNERuntimeTRT::GUID, UNNERuntimeTRT::Version))
	{
		return CanCreateModelStatus::Fail;
	}

	return CanCreateModelStatus::Ok;
}

} // UE::NNERuntimeCoreML::Private::Details

void UNNERuntimeTRT::Init(TSharedRef<UE::NNERuntimeTRT::Private::FEnvironment> InEnvironment)
{
	Environment = InEnvironment;
}

FString UNNERuntimeTRT::GetRuntimeName() const
{
	return TEXT("NNERuntimeTRT");
}

INNERuntime::ECanCreateModelDataStatus UNNERuntimeTRT::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
#if WITH_EDITOR
	return FileType.Compare(TEXT("onnx"), ESearchCase::IgnoreCase) == 0 ? INNERuntime::ECanCreateModelDataStatus::Ok : INNERuntime::ECanCreateModelDataStatus::FailFileIdNotSupported;
#else
	return INNERuntime::ECanCreateModelDataStatus::Fail;
#endif // WITH_EDITOR
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeTRT::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	SCOPED_NAMED_EVENT_TEXT("UNNERuntimeTRT::CreateModelData", FColor::Magenta);

#if WITH_EDITOR
	const FString TargetPlatformName = TargetPlatform ? TargetPlatform->IniPlatformName() : UGameplayStatics::GetPlatformName();
	
	if (CanCreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform) != INNERuntime::ECanCreateModelDataStatus::Ok)
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("UNNERuntimeTRT cannot create the model data with id %s (Filetype: %s) for platform %s"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType, *TargetPlatformName);
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

	TArray64<uint8> ResultData;
	UE::NNERuntimeTRT::Private::FModelData TRTModelData{};

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const bool bIsCooking = TargetPlatform != nullptr;

	const FString FileIdString = FileId.ToString(EGuidFormats::Digits).ToLower();
	const FString IntermediateDirFullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), UE::NNERuntimeTRT::Private::Details::GetIntermediateModelDirPath(TargetPlatformName, GetRuntimeName(), FileIdString)));

	const int32 BuildPrecision = GetTRTBuildPrecision();
	FString TRTEngineFilePath = FPaths::Combine(IntermediateDirFullPath, FileIdString + FString::Printf(TEXT("_p%d"), BuildPrecision)) + ".engine";

	UNNERuntimeTRTSettings* Settings;
	{
		FGCScopeGuard GCGuard;
		Settings = NewObject<UNNERuntimeTRTSettings>();
	}

	bool bNeedCreateEngine = true;

	// If we already have an engine file then just load it
	if (PlatformFile.FileExists(*TRTEngineFilePath) &&
		FFileHelper::LoadFileToArray(ResultData, *TRTEngineFilePath) &&
		TRTModelData.Load(ResultData))
	{
		// If the version of the generated engine file doesn't match our runtime then delete the engine file and recreate it
		if (TRTModelData.GUID == UNNERuntimeTRT::GUID &&
			TRTModelData.Version == UNNERuntimeTRT::Version &&
			TRTModelData.TensorRTVersion == (int32)NV_TENSORRT_VERSION)
		{
			bNeedCreateEngine = false;
		}
	}

	// During cook we always want to create the engine to ensure it's up to date
	if (bNeedCreateEngine || bIsCooking) {
		PlatformFile.DeleteFile(*TRTEngineFilePath);
		TRTModelData = {};
		ResultData.Empty();

		// Create engine file from ONNX model data
		TUniquePtr<nvinfer1::IBuilder> Builder(nvinfer1::createInferBuilder(TRTBuildLogger));
		// TensorRT-RTX does NOT support the kFP16/kINT8 builder flags (deprecated/removed in RTX — see NvInfer.h).
		// The RTX way to run reduced precision is a STRONGLY TYPED network, where precision is taken from the ONNX
		// model's own types. FP32 builds a standard network; FP16/INT8 build strongly typed (honors a reduced-
		// precision ONNX export — with an FP32 model the result is still FP32).
		uint32_t NetworkFlags = 1U << static_cast<int>(nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
		if (BuildPrecision >= 1)
		{
			NetworkFlags |= 1U << static_cast<int>(nvinfer1::NetworkDefinitionCreationFlag::kSTRONGLY_TYPED);
		}
		TUniquePtr<nvinfer1::INetworkDefinition> Network(Builder->createNetworkV2(NetworkFlags));
		TUniquePtr<nvonnxparser::IParser> Parser(nvonnxparser::createParser(*Network, TRTBuildLogger));

		Parser->parse(static_cast<const void *>(FileData.GetData()), FileData.NumBytes());
		
		TUniquePtr<nvinfer1::IBuilderConfig> Config(Builder->createBuilderConfig());
		nvinfer1::IOptimizationProfile* Profile = Builder->createOptimizationProfile();

		for (int32_t InputIndex = 0; InputIndex < Network->getNbInputs(); ++InputIndex)
		{
			nvinfer1::ITensor* Input = Network->getInput(InputIndex);
			nvinfer1::Dims Dims = Input->getDimensions();

			nvinfer1::Dims minDims = Dims;
			nvinfer1::Dims optDims = Dims;
			nvinfer1::Dims maxDims = Dims;

			for (int32_t i = 0; i < Dims.nbDims; i++) {
				if (Dims.d[i] == -1) {
					FTensorRTDimensionRange DimensionRange = Settings->GetDimensionRangeFor(Input->getName(), i);
					minDims.d[i] = DimensionRange.Min;
					optDims.d[i] = DimensionRange.Opt;
					maxDims.d[i] = DimensionRange.Max;
				}
			}

			Profile->setDimensions(Input->getName(), nvinfer1::OptProfileSelector::kMIN, minDims);
			Profile->setDimensions(Input->getName(), nvinfer1::OptProfileSelector::kOPT, optDims);
			Profile->setDimensions(Input->getName(), nvinfer1::OptProfileSelector::kMAX, maxDims);
		}
		
		Config->addOptimizationProfile(Profile);
		Config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kTACTIC_SHARED_MEMORY, 48 << 10);

		// NOTE: no kFP16/kINT8 here — unsupported in TensorRT-RTX. Precision is governed by the strongly-typed
		// network flag above (and RTX already auto-selects fast kernels, e.g. FP16 tensor cores, where beneficial).
		UE_LOG(LogNNERuntimeTRT, Display, TEXT("Building TensorRT engine (precision p%d: 0=FP32 standard, 1/2=strongly typed; RTX derives precision from the model's types)."), BuildPrecision);

		TUniquePtr<nvinfer1::IHostMemory> SerializedEngine(Builder->buildSerializedNetwork(*Network, *Config));
		if (SerializedEngine)
		{
			TRTModelData.GUID = UNNERuntimeTRT::GUID;
			TRTModelData.Version = UNNERuntimeTRT::Version;
			TRTModelData.TensorRTVersion = (int32)NV_TENSORRT_VERSION;
			TRTModelData.EngineFileDataView = TArrayView64<const uint8>(reinterpret_cast<const uint8*>(SerializedEngine->data()), SerializedEngine->size());
			
			TRTModelData.Store(ResultData);

			FFileHelper::SaveArrayToFile(ResultData, *TRTEngineFilePath);
		}
		else
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to build TensorRT engine for model with id %s"), *FileId.ToString(EGuidFormats::Digits));
		}
	}

	// TODO HACK
	if (!PlatformFile.FileExists(*TRTEngineFilePath))
	{
		return {};
	}

	return MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(ResultData)), 0);

#else
	return TSharedPtr<UE::NNE::FSharedModelData>();
#endif // WITH_EDITOR
}

FString UNNERuntimeTRT::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	int32 version = (int32)NV_TENSORRT_VERSION;
	return GetRuntimeName() + "-" + UNNERuntimeTRT::GUID.ToString(EGuidFormats::Digits) + "-" + FString::FromInt(version) + "-p" + FString::FromInt(GetTRTBuildPrecision()) + "-" + FileId.ToString(EGuidFormats::Digits) + "-" + TargetPlatform->PlatformName();
}

UNNERuntimeTRTGpuRdg::ECanCreateModelGPUStatus UNNERuntimeTRTGpuRdg::CanCreateModelGPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	return UE::NNERuntimeTRT::Private::Details::CanCreateModel<UNNERuntimeTRTGpuRdg::ECanCreateModelGPUStatus>(ModelData, GetRuntimeName());
}

TSharedPtr<UE::NNE::IModelGPU> UNNERuntimeTRTGpuRdg::CreateModelGPU(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	if (CanCreateModelGPU(ModelData) != ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("UNNERuntimeTRTGpuRdg cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelGPU>();
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();

	return MakeShared<UE::NNERuntimeTRT::Private::FModelGPU>(Environment.ToSharedRef(), SharedData);
}

UNNERuntimeTRTGpuRdg::ECanCreateModelRDGStatus UNNERuntimeTRTGpuRdg::CanCreateModelRDG(const TObjectPtr<UNNEModelData> ModelData) const
{
	return UE::NNERuntimeTRT::Private::Details::CanCreateModel<UNNERuntimeTRTGpuRdg::ECanCreateModelRDGStatus>(ModelData, GetRuntimeName());
}

TSharedPtr<UE::NNE::IModelRDG> UNNERuntimeTRTGpuRdg::CreateModelRDG(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	if (CanCreateModelRDG(ModelData) != ECanCreateModelRDGStatus::Ok)
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("UNNERuntimeTRTGpuRdg cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelRDG>();
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();
	
	return MakeShared<UE::NNERuntimeTRT::Private::FModelRDG>(Environment.ToSharedRef(), SharedData);
}

#endif // WITH_NNE_RUNTIME_TRT
