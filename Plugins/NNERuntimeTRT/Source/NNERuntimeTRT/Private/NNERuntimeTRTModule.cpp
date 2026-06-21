#include "NNERuntimeTRTModule.h"

#include "Misc/AssertionMacros.h"
#include "Modules/ModuleManager.h"
#include "NNERuntimeTRT.h"
#include "Runtime/CUDA/Source/Public/CudaModule.h"
#include "Runtime/CUDA/Source/Public/CudaWrapper.h"

#ifdef WITH_NNE_RUNTIME_TRT

#include "CoreMinimal.h"
#include "CudaModule.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNERuntimeTRTEnv.h"
#include "RHI.h"
#include "Templates/SharedPointer.h"

#if WITH_EDITOR
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/ConfigCacheIni.h"
#endif // WITH_EDITOR

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#endif // PLATFORM_WINDOWS

#endif // WITH_NNE_RUNTIME_TRT

#include "NNERuntimeTRTLog.h"

// CUDA 12.9 is our minimum required version and we tested up to CUDA 13.1
#define CUDA_MINIMUM_REQUIRED_VERSION 12090
#define CUDA_MAXIMUM_TESTED_VERSION 13010
// TODO we probably need to catch some (future) CUDA versions that are not supported by this TensorRT version?

namespace UE::NNERuntimeTRT::Private
{

namespace Details
{

bool GetDllHandle(const FString& DllPath, TArray<void*>& DllHandles)
{
	void *DllHandle = nullptr;

	if (!FPaths::FileExists(DllPath))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to find the third party library %s."), *DllPath);
		return false;
	}

	DllHandle = FPlatformProcess::GetDllHandle(*DllPath);

	if (!DllHandle)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to load the third party library %s."), *DllPath);
		return false;
	}

	DllHandles.Add(DllHandle);
	return true;
}

bool IsInferenceAvailable(bool bIsCudaAvailable)
{
	if (!IsRHIDeviceNVIDIA())
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("Current RHI device is not NVIDIA, so UNNERuntimeTRT cannot be used for inference."));
		return false;
	}

#if PLATFORM_WINDOWS
	bool bIsD3D12RHIAvailable = IsRHID3D12();
#else
	bool bIsD3D12RHIAvailable = false;
#endif // PLATFORM_WINDOWS
	if (!bIsD3D12RHIAvailable)
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("D3D12 RHI is not available, so UNNERuntimeTRT cannot be used for inference."));
		return false;
	}
	if (!bIsCudaAvailable)
	{
		UE_LOG(LogNNERuntimeTRT, Warning, TEXT("CUDA is not available, so UNNERuntimeTRT cannot be used for inference."));
		return false;
	}
	if(GMaxRHIFeatureLevel != ERHIFeatureLevel::SM6)
	{
		UE_LOG(LogNNERuntimeTRT, Log, TEXT("Minimum feature level required is SM6 for current RHI platform."));
		return false;
	}
	return true;
}

} // namespace Details

}

void FNNERuntimeTRTModule::StartupModule()
{
#ifdef WITH_NNE_RUNTIME_TRT
	using namespace UE::NNERuntimeTRT::Private;

	const FString PluginDir = IPluginManager::Get().FindPlugin("NNERuntimeTRT")->GetBaseDir();
	{
		const FString TensorRTSharedLibPath = FPaths::Combine(PluginDir, TEXT(UE_STRINGIZE(TENSORRT_RTX_TENSORRT_SHAREDLIB_PATH)));
		if (!Details::GetDllHandle(TensorRTSharedLibPath, DllHandles))
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to load TensorRT shared library. TRT Runtimes won't be available."));
			return;
		}
	}
	{
		const FString OnnxparserSharedLibPath = FPaths::Combine(PluginDir, TEXT(UE_STRINGIZE(TENSORRT_RTX_ONNXPARSER_SHAREDLIB_PATH)));
		if (!Details::GetDllHandle(OnnxparserSharedLibPath, DllHandles))
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to load TensorRT OnnxParser shared library. TRT Runtimes won't be available."));
			return;
		}
	}

	FModuleManager::LoadModuleChecked<FCUDAModule>("CUDA").OnPostCUDAInit.AddLambda([this]()
	{
		auto FormatVersion = [](int32 Version) -> FString
		{
			return FString::Printf(TEXT("%d.%d"), Version / 1000, (Version % 1000) / 10);
		};

		FCUDAModule& CUDAModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
		if (CUDAModule.IsAvailable())
		{
			UE_LOG(LogNNERuntimeTRT, Display, TEXT("CUDA module is available, initializing NNE Runtime for TensorRT."));

			const CUDA_DRIVER_API_FUNCTION_LIST* DriverAPI = CUDAModule.DriverAPI();

			// Get driver version
			int32 DriverVersion = 0;
			if (DriverAPI->cuDriverGetVersion(&DriverVersion) == CUDA_SUCCESS)
			{
				if (DriverVersion < CUDA_MINIMUM_REQUIRED_VERSION)
				{
					UE_LOG(LogNNERuntimeTRT, Error, TEXT("CUDA Driver version %s does not meet the minimum required version of %s."),
						*FormatVersion(DriverVersion),
						*FormatVersion(CUDA_MINIMUM_REQUIRED_VERSION));
				}
				else
				{
					bCUDAAvailable = true;
					UE_LOG(LogNNERuntimeTRT, Display, TEXT("CUDA Driver version %s meets the minimum required version of %s."),
						*FormatVersion(DriverVersion),
						*FormatVersion(CUDA_MINIMUM_REQUIRED_VERSION));

					ensureMsgf(DriverVersion <= CUDA_MAXIMUM_TESTED_VERSION, TEXT("CUDA Driver version %s exceeds the maximum tested version of %s."),
						*FormatVersion(DriverVersion),
						*FormatVersion(CUDA_MAXIMUM_TESTED_VERSION));
				}
			}
			else
			{
				UE_LOG(LogNNERuntimeTRT, Warning, TEXT("Failed to get CUDA driver version."));
			}

			RegisterRuntime();
		}
		else
		{
			UE_LOG(LogNNERuntimeTRT, Warning, TEXT("CUDA module is not available, NNE Runtime for TensorRT will not be initialized."));
		}
	});

	RegisterRuntime();

#endif // WITH_NNE_RUNTIME_TRT
}

void FNNERuntimeTRTModule::ShutdownModule()
{
#ifdef WITH_NNE_RUNTIME_TRT
	if (NNERuntimeTRT.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeTRT.Get());
		NNERuntimeTRT->RemoveFromRoot();
		NNERuntimeTRT.Reset();
	}

	// Free the dll handles
	for(void* DllHandle : DllHandles)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
	}

	DllHandles.Empty();
#endif // WITH_NNE_RUNTIME_TRT
}

#ifdef WITH_NNE_RUNTIME_TRT
void FNNERuntimeTRTModule::RegisterRuntime()
{
	using namespace UE::NNERuntimeTRT::Private;

	if (NNERuntimeTRT.IsValid())
	{
		UE::NNE::UnregisterRuntime(NNERuntimeTRT.Get());
		NNERuntimeTRT->RemoveFromRoot();
		NNERuntimeTRT.Reset();
	}

	if (Details::IsInferenceAvailable(bCUDAAvailable))
	{
		NNERuntimeTRT = NewObject<UNNERuntimeTRTGpuRdg>();
	}
#if WITH_EDITOR
	else
	{
		NNERuntimeTRT = NewObject<UNNERuntimeTRT>();
	}
#endif // WITH_EDITOR

	if (NNERuntimeTRT.IsValid())
	{
		FCUDAModule& CUDAModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
		Environment = MakeShared<FEnvironment>(CUDAModule.DriverAPI());

		NNERuntimeTRT->Init(Environment.ToSharedRef());
		NNERuntimeTRT->AddToRoot();
		UE::NNE::RegisterRuntime(NNERuntimeTRT.Get());
	}
}
#endif // WITH_NNE_RUNTIME_TRT
	
IMPLEMENT_MODULE(FNNERuntimeTRTModule, NNERuntimeTRT)
