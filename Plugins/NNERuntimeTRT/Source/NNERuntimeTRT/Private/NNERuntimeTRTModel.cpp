#include "NNERuntimeTRTModel.h"

#include "Misc/ScopeExit.h"
#include "NNERuntimeTRT.h"
#include "NNERuntimeTRTLog.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

#ifdef WITH_NNE_RUNTIME_TRT

#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/UnrealMemory.h"
#include "ID3D12DynamicRHI.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeTRTEnv.h"
#include "NNERuntimeTRTModelData.h"
#include "RenderGraphUtils.h"

// TRT
PRAGMA_DISABLE_DEPRECATION_WARNINGS

BEGIN_SHADER_PARAMETER_STRUCT(FTRTModelInstanceRDGParameters, )
	RDG_BUFFER_ACCESS_ARRAY(InputBuffers)
	RDG_BUFFER_ACCESS_ARRAY(OutputBuffers)
END_SHADER_PARAMETER_STRUCT()

#include <NvInfer.h>

DEFINE_LOG_CATEGORY(LogNNERuntimeTRT);

DECLARE_GPU_STAT_NAMED(FNNERuntimeTRT_RDG, TEXT("FModelInstanceTRT_RDG::EnqueueRDG"));

namespace UE::NNERuntimeTRT
{

namespace Private
{

class FLogger : public nvinfer1::ILogger
{
public:
	void log(Severity severity, const char* msg) noexcept override
	{
		if (severity <= Severity::kWARNING)
		{
			UE_LOG(LogNNERuntimeTRT, Warning, TEXT("%hs"), msg);
		}
	}
} TRTRuntimeLogger;

ENNETensorDataType GetNNETensorDataTypeFromTRTType(nvinfer1::DataType DataType)
{
	switch (DataType)
	{
	case nvinfer1::DataType::kFLOAT:
		return ENNETensorDataType::Float;
	case nvinfer1::DataType::kHALF:
		return ENNETensorDataType::Half;
	case nvinfer1::DataType::kINT8:
		return ENNETensorDataType::Int8;
	case nvinfer1::DataType::kINT32:
		return ENNETensorDataType::Int32;
	case nvinfer1::DataType::kBOOL:
		return ENNETensorDataType::Boolean;
	case nvinfer1::DataType::kUINT8:
		return ENNETensorDataType::UInt8;
	case nvinfer1::DataType::kBF16:
		return ENNETensorDataType::BFloat16;
	case nvinfer1::DataType::kINT64:
		return ENNETensorDataType::Int64;

	case nvinfer1::DataType::kFP8:
	case nvinfer1::DataType::kINT4:
	case nvinfer1::DataType::kFP4:
	default:
		return ENNETensorDataType::None; // unsupported
	};
}

nvinfer1::Dims GetDims(const UE::NNE::FTensorShape& Shape)
{
	nvinfer1::Dims Dims;
	Dims.nbDims = Shape.GetData().Num();
	for (size_t i = 0; i < Dims.nbDims; ++i)
	{
		Dims.d[i] = (int)Shape.GetData()[i];
	}
	return Dims;
}

enum class EDescriptorSource
{
	Engine,
	Context
};

bool GetTensorDescs(const nvinfer1::ICudaEngine* InEngine, const nvinfer1::IExecutionContext* InContext, nvinfer1::TensorIOMode InTensorIOMode, EDescriptorSource InDescriptorSource, TArray<UE::NNE::FTensorDesc>& OutTensorDescs)
{
	OutTensorDescs.Empty();
	int NumBindings = InEngine->getNbIOTensors();
	for (int i = 0; i < NumBindings; ++i)
	{
		const char* Name = InEngine->getIOTensorName(i);
		if (InEngine->getTensorIOMode(Name) != InTensorIOMode)
		{
			continue;
		}
		nvinfer1::DataType DataType = InEngine->getTensorDataType(Name);
		nvinfer1::Dims Dims;
		if (InDescriptorSource == EDescriptorSource::Engine)
		{
			Dims = InEngine->getTensorShape(Name);
		}
		else
		{
			Dims = InContext->getTensorShape(Name);
		}

		TArray<int32> Shape;
		for (int j = 0; j < Dims.nbDims; ++j)
		{
			Shape.Add(static_cast<int32>(Dims.d[j]));
		}

		ENNETensorDataType NNEDataType = Private::GetNNETensorDataTypeFromTRTType(DataType);
		if (NNEDataType == ENNETensorDataType::None)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Unsupported TensorRT data type for tensor %s"), UTF8_TO_TCHAR(Name));
			return false;
		}

		UE::NNE::FTensorDesc TensorDesc = UE::NNE::FTensorDesc::Make(Name, UE::NNE::FSymbolicTensorShape::Make(TConstArrayView<int32>(Shape)), NNEDataType);
		OutTensorDescs.Add(TensorDesc);
	}
	return true;
}

struct FCudaTensorMemory
{
	FCudaTensorMemory() = default;
	FCudaTensorMemory(const CUDA_DRIVER_API_FUNCTION_LIST* InCudaAPI, CUdeviceptr InDevicePtr, TRefCountPtr<FRDGPooledBuffer> InStagingBuffer, CUexternalMemory InExternalMemoryHandle)
		: CudaAPI(InCudaAPI), DevicePtr(InDevicePtr), StagingBuffer(InStagingBuffer), ExternalMemoryHandle(InExternalMemoryHandle), bAvailable(true)
	{

	}

	~FCudaTensorMemory()
	{
		if (DevicePtr && CudaAPI)
		{
			size_t Res = CUDA_SUCCESS;
			FString FunctionName;
			if (ExternalMemoryHandle)
			{
				// If the memory is shared then just clean up the external memory handle
				// The underlying FRDGBuffer destruction will clean up the actual memory
				Res = (size_t)CudaAPI->cuDestroyExternalMemory(ExternalMemoryHandle);
				ExternalMemoryHandle = nullptr;
				FunctionName = TEXT("cuDestroyExternalMemory");
			}
			else
			{
				// Clean up dedicated allocation
				Res = (size_t)CudaAPI->cuMemFree((CUdeviceptr)DevicePtr);
				DevicePtr = 0u;
				FunctionName = TEXT("cuMemFree");
			}
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("%s Error %zu in ~FCudaTensorMemory()"), *FunctionName, (size_t)Res);
			}
		}
	}

	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = nullptr;
	CUdeviceptr DevicePtr = 0u;
	TRefCountPtr<FRDGPooledBuffer> StagingBuffer = nullptr;
	CUexternalMemory ExternalMemoryHandle = nullptr;
	bool bAvailable = true;
};

class IAllocator
{
public:
	virtual ~IAllocator() = default;
	virtual TSharedPtr<FCudaTensorMemory> AllocateTensor(const TCHAR* DebugName, uint64 TensorByteSize) const = 0;
};

class FAllocatorGPU : public IAllocator
{
public:
	FAllocatorGPU(TSharedRef<Private::FEnvironment> InEnvironment) : Environment(InEnvironment)
	{

	}
	virtual ~FAllocatorGPU() = default;

	virtual TSharedPtr<FCudaTensorMemory> AllocateTensor(const TCHAR* DebugName, uint64 TensorByteSize) const override
	{
		auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
		const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

		CUdeviceptr DevicePtr = 0;
		CUresult Res = CudaAPI->cuMemAlloc(&DevicePtr, TensorByteSize);
		if (Res != CUDA_SUCCESS)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuMemAlloc Error %zu allocating buffer %s of size %zu bytes"), (size_t)Res, DebugName, (size_t)TensorByteSize);
			return nullptr;
		}
		return MakeShared<FCudaTensorMemory>(CudaAPI, DevicePtr, nullptr, nullptr);
	}

private:
	TSharedRef<Private::FEnvironment> Environment;
};

class FAllocatorRDG : public IAllocator
{
public:
	FAllocatorRDG(TSharedRef<Private::FEnvironment> InEnvironment) : Environment(InEnvironment)
	{

	}
	virtual ~FAllocatorRDG() = default;

	virtual TSharedPtr<FCudaTensorMemory> AllocateTensor(const TCHAR* DebugName, uint64 TensorByteSize) const override
	{
		auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
		const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

		// Allocate a shared RDG buffer
		FRDGBufferDesc RDGBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(TensorByteSize);
		RDGBufferDesc.Usage |= EBufferUsageFlags::Shared;

		FString PooledBufferName = FString(DebugName) + FString(TEXT("_PooledBuffer"));
		TRefCountPtr<FRDGPooledBuffer> StagingBuffer = AllocatePooledBuffer(RDGBufferDesc, *PooledBufferName);

		FRHIBuffer* RHIBuffer = StagingBuffer->GetRHI();
		ID3D12Resource* D3D12Resource = GetID3D12DynamicRHI()->RHIGetResource(RHIBuffer);
		size_t RHIBufferSize = GetID3D12DynamicRHI()->RHIGetResourceMemorySize(RHIBuffer);

		HANDLE SharedHandle = nullptr;
		GetID3D12DynamicRHI()->RHIGetDevice(0)->CreateSharedHandle(D3D12Resource, nullptr, GENERIC_ALL, nullptr, &SharedHandle);

		if (SharedHandle == nullptr)
		{
			UE_LOG(LogNNERuntimeTRT, Warning, TEXT("Creation of shared handle for %s failed."), DebugName);
			return nullptr;
		}
		// Import the shared handle into CUDA as an external memory
		CUexternalMemory Handle = nullptr;
		{
			CUDA_EXTERNAL_MEMORY_HANDLE_DESC Desc{};
			Desc.type = CUexternalMemoryHandleType::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
			Desc.handle.win32.handle = SharedHandle;
			Desc.size = RHIBufferSize;
			Desc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
			CUresult Res = CudaAPI->cuImportExternalMemory(&Handle, &Desc);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuImportExternalMemory Error %zu"), (size_t)Res);
				return nullptr;
			}
		}

		// Get the device pointer from the external memory handle
		CUdeviceptr DevicePtr = 0u;
		{
			CUDA_EXTERNAL_MEMORY_BUFFER_DESC Desc{};
			Desc.flags = 0u;
			Desc.offset = 0u;
			Desc.size = RHIBufferSize;
			CUresult Res = CudaAPI->cuExternalMemoryGetMappedBuffer(&DevicePtr, Handle, &Desc);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuExternalMemoryGetMappedBuffer Error %zu"), (size_t)Res);
				return nullptr;
			}
		}
		return MakeShared<FCudaTensorMemory>(CudaAPI, DevicePtr, StagingBuffer, Handle);
	}

private:
	TSharedRef<Private::FEnvironment> Environment;
};

class FMemoryPool
{
public:
	FMemoryPool(const IAllocator* InAllocator) : Allocator(InAllocator) {}
	~FMemoryPool() = default;

	TSharedPtr<FCudaTensorMemory> GetOrAllocate(const TCHAR* TensorName, uint64 TensorByteSize)
	{
		// Try to find a compatible, available tensor in the pool
		for (auto& TensorMemory : TensorMemoryPool)
		{
			if (TensorMemory.Key == TensorByteSize && TensorMemory.Value->bAvailable)
			{
				TensorMemory.Value->bAvailable = false;
				return TensorMemory.Value;
			}
		}

		TSharedPtr<FCudaTensorMemory> TensorMemory = Allocator->AllocateTensor(TensorName, TensorByteSize);
		if (TensorMemory)
		{
			TensorMemory->bAvailable = false;
			TensorMemoryPool.Emplace(TensorByteSize, TensorMemory);
		}
		return TensorMemory;
	}

	void ReleaseAll()
	{
		for (auto& Tensor : TensorMemoryPool)
		{
			Tensor.Value->bAvailable = true;
		}
	}

private:
	const IAllocator* Allocator = nullptr;
	TArray<TPair<uint64, TSharedPtr<FCudaTensorMemory>>> TensorMemoryPool;
};

FModelInstanceGPU::FModelInstanceGPU(TSharedRef<Private::FEnvironment> InEnvironment) : Environment(InEnvironment)
{
	Allocator = MakeUnique<Private::FAllocatorGPU>(InEnvironment);
	MemoryPool = MakeUnique<Private::FMemoryPool>(Allocator.Get());
}

FModelInstanceGPU::~FModelInstanceGPU()
{
	{
		auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
		const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

		if (Context)
		{
			delete Context;
		}
		if (Engine)
		{
			delete Engine;
		}
		if (Runtime)
		{
			delete Runtime;
		}
		if (CudaStream && CudaAPI)
		{
			CUresult Res = CudaAPI->cuStreamDestroy(CudaStream);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuStreamDestroy Error %zu"), (size_t)Res);
			}
		}

		MemoryPool.Reset();
	}
}

bool FModelInstanceGPU::Init(TConstArrayView64<uint8> ModelData)
{
	UE::NNERuntimeTRT::Private::FModelData TRTModelData;
	if (!TRTModelData.Load(ModelData))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to read model data."));
		return false;
	}

	if (TRTModelData.TensorRTVersion != (int32)NV_TENSORRT_VERSION)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Model was created with TensorRT version %d, but runtime is using version %d"), TRTModelData.TensorRTVersion, (int32)NV_TENSORRT_VERSION);
		return false;
	}

	auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

	Runtime = nvinfer1::createInferRuntime(Private::TRTRuntimeLogger);
	if (!Runtime)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to create TensorRT runtime"));
		return false;
	}

	Engine = Runtime->deserializeCudaEngine(TRTModelData.EngineFileDataView.GetData(), TRTModelData.EngineFileDataView.NumBytes());
	if (!Engine)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to deserialize CUDA engine"));
		return false;
	}

	Context = Engine->createExecutionContext();
	if (!Context)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to create execution context"));
		return false;
	}

	CUresult Res = CudaAPI->cuStreamCreate(&CudaStream, 0);
	if (Res != CUDA_SUCCESS)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuStreamCreate Error %zu"), (size_t)Res);
		return false;
	}

	if (!Private::GetTensorDescs(Engine, Context, nvinfer1::TensorIOMode::kINPUT, Private::EDescriptorSource::Engine, InputTensorDescs))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get input tensor descriptors"));
		return false;
	}
	if (!Private::GetTensorDescs(Engine, Context, nvinfer1::TensorIOMode::kOUTPUT, Private::EDescriptorSource::Engine, OutputTensorDescs))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get output tensor descriptors"));
		return false;
	}

	return true;
}

TConstArrayView<UE::NNE::FTensorDesc> FModelInstanceGPU::GetInputTensorDescs() const
{
	return TConstArrayView<UE::NNE::FTensorDesc>(InputTensorDescs);
}
TConstArrayView<UE::NNE::FTensorDesc> FModelInstanceGPU::GetOutputTensorDescs() const
{
	return TConstArrayView<UE::NNE::FTensorDesc>(OutputTensorDescs);
}
TConstArrayView<UE::NNE::FTensorShape> FModelInstanceGPU::GetInputTensorShapes() const
{
	return TConstArrayView<UE::NNE::FTensorShape>(InputTensorShapes);
}
TConstArrayView<UE::NNE::FTensorShape> FModelInstanceGPU::GetOutputTensorShapes() const
{
	return TConstArrayView<UE::NNE::FTensorShape>(OutputTensorShapes);
}

FModelInstanceGPU::ESetInputTensorShapesStatus FModelInstanceGPU::SetInputTensorShapes(TConstArrayView<UE::NNE::FTensorShape> InInputShapes)
{
	if (InInputShapes.Num() != InputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor shape count mismatch: expected %d, got %d"), InputTensorDescs.Num(), InInputShapes.Num());
		return ESetInputTensorShapesStatus::Fail;
	}
	for (int32 i = 0; i < InInputShapes.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& Desc = InputTensorDescs[i];
		if (!InInputShapes[i].IsCompatibleWith(Desc.GetShape()))
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input shape does not match tensor %s"), *Desc.GetName());
			return ESetInputTensorShapesStatus::Fail;
		}
	}
	InputTensorShapes.Reset();
	for (const UE::NNE::FTensorShape& Shape : InInputShapes)
	{
		InputTensorShapes.Add(Shape);
	}
	for (int32 i = 0; i < InInputShapes.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& Desc = InputTensorDescs[i];
		bool bRes = Context->setInputShape(TCHAR_TO_UTF8(*Desc.GetName()), Private::GetDims(InInputShapes[i]));
		if (!bRes)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to set input shape for tensor %s"), *Desc.GetName());
			return ESetInputTensorShapesStatus::Fail;
		}
	}

	// Now that we have set the concrete input shapes we can fetch the concrete output shapes from the execution context
	if (!Private::GetTensorDescs(Engine, Context, nvinfer1::TensorIOMode::kOUTPUT, Private::EDescriptorSource::Context, OutputTensorDescs))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get output tensor descriptors from execution context"));
		return ESetInputTensorShapesStatus::Fail;
	}
	OutputTensorShapes.Reset();
	for (const UE::NNE::FTensorDesc& Desc : OutputTensorDescs)
	{
		OutputTensorShapes.Add(UE::NNE::FTensorShape::MakeFromSymbolic(Desc.GetShape()));
	}

	return ESetInputTensorShapesStatus::Ok;
}

FModelInstanceGPU::ERunSyncStatus FModelInstanceGPU::RunSync(TConstArrayView<UE::NNE::FTensorBindingCPU> InInputTensors, TConstArrayView<UE::NNE::FTensorBindingCPU> InOutputTensors)
{
	auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

	ON_SCOPE_EXIT
	{
		MemoryPool->ReleaseAll();
	};

	if (InputTensorShapes.IsEmpty())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor shapes not set. Call SetInputTensorShapes before RunSync."));
		return ERunSyncStatus::Fail;
	}

	if (!Context)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("TensorRT RTX Context invalid"));
		return ERunSyncStatus::Fail;
	}

	if (!CudaStream)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("CUDA Stream invalid"));
		return ERunSyncStatus::Fail;
	}

	if (InInputTensors.Num() != InputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor count mismatch: expected %d, got %d"), InputTensorDescs.Num(), InInputTensors.Num());
		return ERunSyncStatus::Fail;
	}

	if (!InOutputTensors.IsEmpty() && InOutputTensors.Num() != OutputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Output tensor count mismatch: expected %d, got %d"), OutputTensorDescs.Num(), InOutputTensors.Num());
		return ERunSyncStatus::Fail;
	}

	// Copy input tensors to device memory
	for (int32 i = 0; i < InInputTensors.Num(); ++i)
	{
		if (!InInputTensors[i].Data && InInputTensors[i].SizeInBytes != 0)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Binding input tensor %d is not set but given size is non-zero %" UINT64_FMT "."), i, InInputTensors[i].SizeInBytes);
			return ERunSyncStatus::Fail;
		}

		const UE::NNE::FTensorDesc& TensorDesc = InputTensorDescs[i];
		const UE::NNE::FTensorShape& TensorShape = InputTensorShapes[i];
		const uint64 ExpectedSizeInBytes = TensorDesc.GetElementByteSize() * TensorShape.Volume();

		if (InInputTensors[i].SizeInBytes != ExpectedSizeInBytes)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor %d size mismatch: expected %" UINT64_FMT ", got %" UINT64_FMT "."), i, ExpectedSizeInBytes, InInputTensors[i].SizeInBytes);
			return ERunSyncStatus::Fail;
		}

		TSharedPtr<Private::FCudaTensorMemory> TensorMemory = MemoryPool->GetOrAllocate(*TensorDesc.GetName(), InInputTensors[i].SizeInBytes);
		if (TensorMemory == nullptr)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to allocate device memory for tensor %s"), *TensorDesc.GetName());
			return ERunSyncStatus::Fail;
		}

		CUresult Res = CudaAPI->cuMemcpyHtoDAsync(TensorMemory->DevicePtr, InInputTensors[i].Data, InInputTensors[i].SizeInBytes, CudaStream);
		if (Res != CUDA_SUCCESS)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuMemcpyHtoDAsync Error %zu for tensor %s"), (size_t)Res, *TensorDesc.GetName());
			return ERunSyncStatus::Fail;
		}
		Context->setInputTensorAddress(TCHAR_TO_UTF8(*TensorDesc.GetName()), (void*)TensorMemory->DevicePtr);
	}

	// Set output device pointer for context and bind to host memory for readback
	TArray<CUdeviceptr> CudaOutputTensors;
	for (int32 i = 0; i < InOutputTensors.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& TensorDesc = OutputTensorDescs[i];
		const UE::NNE::FTensorShape& TensorShape = OutputTensorShapes[i];
		TSharedPtr<Private::FCudaTensorMemory> TensorMemory = MemoryPool->GetOrAllocate(*TensorDesc.GetName(), InOutputTensors[i].SizeInBytes);
		if (TensorMemory == nullptr)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to allocate device memory for output tensor %s"), *TensorDesc.GetName());
			return ERunSyncStatus::Fail;
		}
		Context->setOutputTensorAddress(TCHAR_TO_UTF8(*TensorDesc.GetName()), (void*)TensorMemory->DevicePtr);
		CudaOutputTensors.Add(TensorMemory->DevicePtr);
	}

	// Run model
	if (!Context->enqueueV3(CudaStream))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to enqueue model"));
		return ERunSyncStatus::Fail;
	}

	// Copy output tensors from device memory
	for (int32 i = 0; i < CudaOutputTensors.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& TensorDesc = OutputTensorDescs[i];
		const UE::NNE::FTensorShape& TensorShape = OutputTensorShapes[i];
		const uint64 ExpectedSizeInBytes = TensorDesc.GetElementByteSize() * TensorShape.Volume();

		if (InOutputTensors[i].Data &&
			ExpectedSizeInBytes > 0 &&
			InOutputTensors[i].SizeInBytes >= ExpectedSizeInBytes)
		{
			CUresult Res = CudaAPI->cuMemcpyDtoHAsync(InOutputTensors[i].Data, CudaOutputTensors[i], ExpectedSizeInBytes, CudaStream);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuMemcpyDtoHAsync Error %zu"), (size_t)Res);
				return ERunSyncStatus::Fail;
			}
		}
	}
	CudaAPI->cuStreamSynchronize(CudaStream);

	return ERunSyncStatus::Ok;
}

FModelGPU::FModelGPU(TSharedRef<Private::FEnvironment> InEnvironment, TSharedRef<NNE::FSharedModelData> InModelData) : Environment(InEnvironment), ModelData(InModelData)
{

}

TSharedPtr<UE::NNE::IModelInstanceGPU> FModelGPU::CreateModelInstanceGPU()
{
	TSharedPtr<FModelInstanceGPU> ModelInstance = MakeShared<FModelInstanceGPU>(Environment);
	if (!ModelInstance->Init(ModelData->GetView()))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Cannot initialize model instance"));
		return {};
	}

	return ModelInstance;
}

struct FCudaGraphExecutionContext
{
	FCudaGraphExecutionContext(const CUDA_DRIVER_API_FUNCTION_LIST* InCudaAPI) : CudaAPI(InCudaAPI)
	{

	}

	~FCudaGraphExecutionContext()
	{
		if (CudaGraphExec && CudaAPI)
		{
			CUresult Res = CudaAPI->cuGraphExecDestroy(CudaGraphExec);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuGraphExecDestroy Error %zu"), (size_t)Res);
			}
			CudaGraphExec = nullptr;
		}
		if (CudaGraph && CudaAPI)
		{
			CUresult Res = CudaAPI->cuGraphDestroy(CudaGraph);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuGraphDestroy Error %zu"), (size_t)Res);
			}
			CudaGraph = nullptr;
		}
	}

	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = nullptr;
	CUgraph CudaGraph = nullptr;
	CUgraphExec CudaGraphExec = nullptr;
	bool bCudaGraphCaptured = false;
	TArray<void*> InputAddresses;
	TArray<void*> OutputAddresses;
};

class FModelInstanceRDG::FProxy
{
public:
	FProxy(TSharedRef<Private::FEnvironment> InEnvironment) : Environment(InEnvironment)
	{
		Allocator = MakeUnique<Private::FAllocatorRDG>(InEnvironment);
		MemoryPool = MakeUnique<Private::FMemoryPool>(Allocator.Get());
	}

	~FProxy()
	{
		auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
		const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

		if (Context)
		{
			delete Context;
		}
		if (Engine)
		{
			delete Engine;
		}
		if (Runtime)
		{
			delete Runtime;
		}
		if (CudaStream && CudaAPI)
		{
			CUresult Res = CudaAPI->cuStreamDestroy(CudaStream);
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuStreamDestroy Error %zu"), (size_t)Res);
			}
		}

		MemoryPool.Reset();
		CudaGraphExecutionContexts.Empty();

		if (CudaSemaphore)
		{
			CUresult Res = CudaAPI->cuDestroyExternalSemaphore(CudaSemaphore);
			CudaSemaphore = nullptr;
			if (Res != CUDA_SUCCESS)
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuDestroyExternalSemaphore Error %zu"), (size_t)Res);
			}
		}
	}

	void SetOutputTensorShapes(TConstArrayView<NNE::FTensorShape> Shapes)
	{
		FScopeLock Lock(&CriticalSection);

		OutputTensorShapes = Shapes;
	}

	TConstArrayView<NNE::FTensorShape> GetOutputTensorShapes() const
	{
		FScopeLock Lock(&CriticalSection);

		return OutputTensorShapes;
	}

	TSharedRef<Private::FEnvironment> Environment;

	nvinfer1::IRuntime* Runtime = nullptr;
	nvinfer1::IExecutionContext* Context = nullptr;
	nvinfer1::ICudaEngine* Engine = nullptr;
	CUstream CudaStream = nullptr;

	TUniquePtr<Private::IAllocator> Allocator;
	TUniquePtr<Private::FMemoryPool> MemoryPool;

	// Cuda graph support
	bool bUseCudaGraph = true;

	// One graph execution context per unique set of input and output buffers
	TArray<TSharedPtr<FCudaGraphExecutionContext>> CudaGraphExecutionContexts;

	ID3D12Fence* Fence = nullptr;
	void* FenceSharedHandle = nullptr;
	CUexternalSemaphore CudaSemaphore = nullptr;

	TArray<UE::NNE::FTensorDesc> InputTensorDescs;
	TArray<UE::NNE::FTensorDesc> OutputTensorDescs;

private:
	mutable FCriticalSection CriticalSection;
	TArray<UE::NNE::FTensorShape> OutputTensorShapes;
};

FModelInstanceRDG::FModelInstanceRDG(TSharedRef<Private::FEnvironment> InEnvironment) : Environment(InEnvironment)
{
	Proxy = MakeShared<FProxy>(InEnvironment);
}

FModelInstanceRDG::~FModelInstanceRDG()
{

}

bool FModelInstanceRDG::Init(TConstArrayView64<uint8> ModelData)
{
	UE::NNERuntimeTRT::Private::FModelData TRTModelData;
	if (!TRTModelData.Load(ModelData))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to read model data."));
		return false;
	}

	if (TRTModelData.TensorRTVersion != (int32)NV_TENSORRT_VERSION)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Model was created with TensorRT version %d, but runtime is using version %d"), TRTModelData.TensorRTVersion, (int32)NV_TENSORRT_VERSION);
		return false;
	}

	auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

	Proxy->Runtime = nvinfer1::createInferRuntime(Private::TRTRuntimeLogger);
	if (!Proxy->Runtime)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to create TensorRT runtime"));
		return false;
	}

	Proxy->Engine = Proxy->Runtime->deserializeCudaEngine(TRTModelData.EngineFileDataView.GetData(), TRTModelData.EngineFileDataView.NumBytes());
	if (!Proxy->Engine)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to deserialize CUDA engine"));
		return false;
	}

	Proxy->Context = Proxy->Engine->createExecutionContext();
	if (!Proxy->Context)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to create execution context"));
		return false;
	}

	CUresult Res = CudaAPI->cuStreamCreate(&Proxy->CudaStream, 0);
	if (Res != CUDA_SUCCESS)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuStreamCreate Error %zu"), (size_t)Res);
		return false;
	}

	if (!Private::GetTensorDescs(Proxy->Engine, Proxy->Context, nvinfer1::TensorIOMode::kINPUT, Private::EDescriptorSource::Engine, Proxy->InputTensorDescs))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get input tensor descriptors"));
		return false;
	}
	if (!Private::GetTensorDescs(Proxy->Engine, Proxy->Context, nvinfer1::TensorIOMode::kOUTPUT, Private::EDescriptorSource::Engine, Proxy->OutputTensorDescs))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get output tensor descriptors"));
		return false;
	}

	InitialInputTensorDescs = Proxy->InputTensorDescs;
	InitialOutputTensorDescs = Proxy->OutputTensorDescs;

	return true;
}

TConstArrayView<UE::NNE::FTensorDesc> FModelInstanceRDG::GetInputTensorDescs() const
{
	return TConstArrayView<UE::NNE::FTensorDesc>(InitialInputTensorDescs);
}
TConstArrayView<UE::NNE::FTensorDesc> FModelInstanceRDG::GetOutputTensorDescs() const
{
	return TConstArrayView<UE::NNE::FTensorDesc>(InitialOutputTensorDescs);
}
TConstArrayView<UE::NNE::FTensorShape> FModelInstanceRDG::GetInputTensorShapes() const
{
	return TConstArrayView<UE::NNE::FTensorShape>(InputTensorShapes);
}
TConstArrayView<UE::NNE::FTensorShape> FModelInstanceRDG::GetOutputTensorShapes() const
{
	return TConstArrayView<UE::NNE::FTensorShape>(Proxy->GetOutputTensorShapes());
}

FModelInstanceRDG::ESetInputTensorShapesStatus FModelInstanceRDG::SetInputTensorShapes(TConstArrayView<UE::NNE::FTensorShape> InInputShapes)
{
	if (IsInRenderingThread())
	{
		return SetInputTensorShapes_RenderThread(InInputShapes);
	}
	else
	{
		check(IsInGameThread());

		FEvent*	Signal = FGenericPlatformProcess::GetSynchEventFromPool();
		ESetInputTensorShapesStatus Result = ESetInputTensorShapesStatus::Ok;

		ENQUEUE_RENDER_COMMAND(FModelInstanceTRTRDG_SetInputTensorShapes)([this, InInputShapes, Signal, &Result](FRHICommandListImmediate& RHICmdList)
		{
			Result = SetInputTensorShapes_RenderThread(InInputShapes);

			// note: Block here, if SetInputTensorShapes does actual GPU work!
			Signal->Trigger();
		});

		Signal->Wait();

		FGenericPlatformProcess::ReturnSynchEventToPool(Signal);

		return Result;
	}
}

FModelInstanceRDG::ESetInputTensorShapesStatus FModelInstanceRDG::SetInputTensorShapes_RenderThread(TConstArrayView<UE::NNE::FTensorShape> InInputShapes)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

	Proxy->SetOutputTensorShapes({});

	if (InInputShapes.Num() != InitialInputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor shape count mismatch: expected %d, got %d"), InitialInputTensorDescs.Num(), InInputShapes.Num());
		return ESetInputTensorShapesStatus::Fail;
	}

	for (int32 i = 0; i < InInputShapes.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& Desc = InitialInputTensorDescs[i];
		if (!InInputShapes[i].IsCompatibleWith(Desc.GetShape()))
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input shape does not match tensor %s"), *Desc.GetName());
			return ESetInputTensorShapesStatus::Fail;
		}
	}

	InputTensorShapes.Reset();
	for (const UE::NNE::FTensorShape& Shape : InInputShapes)
	{
		InputTensorShapes.Add(Shape);
	}

	RHICmdList.EnqueueLambda([Proxy = Proxy, InputTensorShapes = InputTensorShapes](FRHICommandListImmediate& RHICmdList)
	{
		GetID3D12PlatformDynamicRHI()->RHIRunOnQueue(ED3D12RHIRunOnQueueType::Graphics, [Proxy = Proxy, InputTensorShapes = InputTensorShapes] (ID3D12CommandQueue* D3D12CommandQueue)
		{
			SCOPED_NAMED_EVENT_TEXT("FModelInstanceTRTRDG::SetInputTensorShapes RHIRunOnQueue", FColor::Magenta);

			for (int32 i = 0; i < InputTensorShapes.Num(); ++i)
			{
				UE::NNE::FTensorDesc& TensorDesc = Proxy->InputTensorDescs[i];
				bool bRes = Proxy->Context->setInputShape(TCHAR_TO_UTF8(*TensorDesc.GetName()), Private::GetDims(InputTensorShapes[i]));
				if (!bRes)
				{
					UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to set input shape for tensor %s"), *TensorDesc.GetName());
					return;
				}
			}

			// Now that we have set the concrete input shapes we can fetch the concrete output shapes from the execution context
			TArray<UE::NNE::FTensorDesc> OutputTensorDescs;
			if (!Private::GetTensorDescs(Proxy->Engine, Proxy->Context, nvinfer1::TensorIOMode::kOUTPUT, Private::EDescriptorSource::Context, OutputTensorDescs))
			{
				UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to get output tensor descriptors from execution context"));
				return;
			}

			TArray<NNE::FTensorShape> ResultOutputTensorShapes;
			for (const UE::NNE::FTensorDesc& TensorDesc : OutputTensorDescs)
			{
				ResultOutputTensorShapes.Add(UE::NNE::FTensorShape::MakeFromSymbolic(TensorDesc.GetShape()));
			}

			Proxy->SetOutputTensorShapes(ResultOutputTensorShapes);
		}, false);
	});

	return ESetInputTensorShapesStatus::Ok;
}

FModelInstanceRDG::EEnqueueRDGStatus FModelInstanceRDG::EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<UE::NNE::FTensorBindingRDG> InInputBindings, TConstArrayView<UE::NNE::FTensorBindingRDG> InOutputBindings)
{
	SCOPED_NAMED_EVENT_TEXT("FModelInstanceTRT_RDG::EnqueueRDG", FColor::Magenta);

	auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

	if (!CreateSyncPrimitives())
	{
		return EEnqueueRDGStatus::Fail;
	}

	if (InputTensorShapes.IsEmpty())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input tensor shapes not set. Call SetInputTensorShapes before EnqueueRDG."));
		return EEnqueueRDGStatus::Fail;
	}

	if (!Proxy->Context)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("TensorRT RTX Context invalid"));
		return EEnqueueRDGStatus::Fail;
	}

	if (!Proxy->CudaStream)
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("CUDA Stream invalid"));
		return EEnqueueRDGStatus::Fail;
	}

	if (InInputBindings.Num() != InitialInputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Input bindings need to match input tensor descriptor count (got %d, expected %d)."), InInputBindings.Num(), InitialInputTensorDescs.Num());
		return EEnqueueRDGStatus::Fail;
	}

	if (!InOutputBindings.IsEmpty() && InOutputBindings.Num() != InitialOutputTensorDescs.Num())
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Output binding can be empty or needs to match output tensor descriptor count (got %d, expected %d)."), InOutputBindings.Num(), InitialOutputTensorDescs.Num());
		return EEnqueueRDGStatus::Fail;
	}

	TArray<TSharedPtr<Private::FCudaTensorMemory>> InputCudaTensors;
	TArray<TSharedPtr<Private::FCudaTensorMemory>> OutputCudaTensors;

	FTRTModelInstanceRDGParameters* PassParameters = RDGBuilder.AllocParameters<FTRTModelInstanceRDGParameters>();

	// For each input, get a tensor buffer from the pool and copy the input data into it
	// Then bind this memory to the model
	for (int32 i = 0; i < InInputBindings.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& TensorDesc = InitialInputTensorDescs[i];

		TSharedPtr<Private::FCudaTensorMemory> CudaTensor = Proxy->MemoryPool->GetOrAllocate(*TensorDesc.GetName(), InInputBindings[i].Buffer->Desc.GetSize());
		if (!CudaTensor)
		{
			return EEnqueueRDGStatus::Fail;
		}

		FRDGBufferRef RDGBuffer = RDGBuilder.RegisterExternalBuffer(CudaTensor->StagingBuffer, ERDGBufferFlags::None);
		AddCopyBufferPass(RDGBuilder, RDGBuffer, 0, InInputBindings[i].Buffer, 0, RDGBuffer->GetSize());

		InputCudaTensors.Add(CudaTensor);
		PassParameters->InputBuffers.Emplace(RDGBuffer, ERHIAccess::CopySrc);
	}

	// Record output buffers mapping for enqueing copy passes after model execution
	struct FRDGBufferMapping { FRDGBufferRef Dest; FRDGBufferRef Src; };
	TArray<FRDGBufferMapping> OutputBuffersToCopy;

	// For each output, get a tensor buffer from the pool and bind it to the model
	for (int32 i = 0; i < InOutputBindings.Num(); ++i)
	{
		const UE::NNE::FTensorDesc& TensorDesc = InitialOutputTensorDescs[i];

		TSharedPtr<Private::FCudaTensorMemory> CudaTensor = Proxy->MemoryPool->GetOrAllocate(*TensorDesc.GetName(), InOutputBindings[i].Buffer->Desc.GetSize());
		if (!CudaTensor)
		{
			return EEnqueueRDGStatus::Fail;
		}

		FRDGBufferRef RDGBuffer = RDGBuilder.RegisterExternalBuffer(CudaTensor->StagingBuffer, ERDGBufferFlags::None);
		OutputBuffersToCopy.Add(FRDGBufferMapping{ InOutputBindings[i].Buffer, RDGBuffer });

		OutputCudaTensors.Add(CudaTensor);
		PassParameters->OutputBuffers.Emplace(RDGBuffer, ERHIAccess::CopyDest);
	}

	RDG_EVENT_SCOPE_STAT(RDGBuilder, FNNERuntimeTRT_RDG, "FModelInstanceTRT_RDG::EnqueueRDG");

	RDGBuilder.AddPass(RDG_EVENT_NAME("FModelInstanceTRTRDG::EnqueueRDG.AddPass"), PassParameters, ERDGPassFlags::Readback,
	[PassParameters, Proxy = Proxy, InputCudaTensors, OutputCudaTensors, SemaphoreCounter = SemaphoreCounter](FRHICommandListImmediate& RHICmdList)
	{
		// Submit previous work here to the GPU to avoid ORT Session Run() dispatching its work first
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

		RHICmdList.EnqueueLambda([Proxy, InputCudaTensors, OutputCudaTensors, SemaphoreCounter](FRHICommandListImmediate& RHICmdList)
		{
			GetID3D12PlatformDynamicRHI()->RHIRunOnQueue(ED3D12RHIRunOnQueueType::Graphics, [Proxy, InputCudaTensors, OutputCudaTensors, SemaphoreCounter] (ID3D12CommandQueue* D3D12CommandQueue)
			{
				SCOPED_NAMED_EVENT_TEXT("FModelInstanceTRTRDG::EnqueueRDG.AddPass RHIRunOnQueue", FColor::Magenta);

				auto ScopedCudaContext = Proxy->Environment->ActivateScopedCudaContext();
				const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Proxy->Environment->GetCudaAPI();

				TArray<void*> InputAddressesArray;
				for (int32 i = 0; i < InputCudaTensors.Num(); ++i)
				{
					InputAddressesArray.Add((void*)InputCudaTensors[i]->DevicePtr);
				}

				TArray<void*> OutputAddressesArray;
				for (int32 i = 0; i < OutputCudaTensors.Num(); ++i)
				{
					OutputAddressesArray.Add((void*)OutputCudaTensors[i]->DevicePtr);
				}

				auto SetInputOutputAddresses = [&Proxy, &InputCudaTensors, &OutputCudaTensors, &InputAddressesArray, &OutputAddressesArray]()
				{
					for (int32 i = 0; i < InputCudaTensors.Num(); ++i)
					{
						Proxy->Context->setInputTensorAddress(TCHAR_TO_UTF8(*Proxy->InputTensorDescs[i].GetName()), InputAddressesArray[i]);
					}
					for (int32 i = 0; i < OutputCudaTensors.Num(); ++i)
					{
						Proxy->Context->setOutputTensorAddress(TCHAR_TO_UTF8(*Proxy->OutputTensorDescs[i].GetName()), OutputAddressesArray[i]);
					}
				};

				D3D12CommandQueue->Signal(Proxy->Fence, SemaphoreCounter);

				// Wait on the fence for D3D work to be done
				CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS WaitParams = {};
				WaitParams.params.fence.value = SemaphoreCounter;
				CudaAPI->cuWaitExternalSemaphoresAsync(&Proxy->CudaSemaphore, &WaitParams, 1, Proxy->CudaStream);

				TSharedPtr<FCudaGraphExecutionContext> GraphExecutionContext;
				if (Proxy->bUseCudaGraph)
				{
					// Find a matching graph execution context if there is one
					auto GraphExecutionContextIt = Proxy->CudaGraphExecutionContexts.FindByPredicate(
						[&InputAddressesArray, &OutputAddressesArray](const TSharedPtr<FCudaGraphExecutionContext>& Context)
						{
							return Context->InputAddresses == InputAddressesArray && Context->OutputAddresses == OutputAddressesArray;
						}
					);
					// Create a graph execution context if one does not exist
					if (GraphExecutionContextIt == nullptr)
					{
						TSharedPtr<FCudaGraphExecutionContext> NewContext = MakeShared<FCudaGraphExecutionContext>(CudaAPI);
						NewContext->InputAddresses = InputAddressesArray;
						NewContext->OutputAddresses = OutputAddressesArray;
						Proxy->CudaGraphExecutionContexts.Add(NewContext);
						GraphExecutionContextIt = &Proxy->CudaGraphExecutionContexts.Last();
					}
					GraphExecutionContext = *GraphExecutionContextIt;

					if (!GraphExecutionContext->bCudaGraphCaptured)
					{
						Proxy->bUseCudaGraph = false;
						// Create a stream for capturing the graph
						CUstream CaptureStream;
						CudaAPI->cuStreamCreate(&CaptureStream, 0);
						CUresult Res = CudaAPI->cuStreamBeginCapture(CaptureStream, CU_STREAM_CAPTURE_MODE_GLOBAL);
						if (Res != CUDA_SUCCESS)
						{
							UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuStreamBeginCapture Error %zu"), (size_t)Res);
						}
						else
						{
							// Set the I/O tensor addresses and enqueue the model on the stream
							SetInputOutputAddresses();
							if (!Proxy->Context->enqueueV3(CaptureStream))
							{
								UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to enqueue model for CUDA graph capture"));
							}
							Res = CudaAPI->cuStreamEndCapture(CaptureStream, &GraphExecutionContext->CudaGraph);
							if (Res == CUDA_SUCCESS)
							{
								Res = CudaAPI->cuGraphInstantiate(&GraphExecutionContext->CudaGraphExec, GraphExecutionContext->CudaGraph, nullptr, nullptr, 0);
								if (Res != CUDA_SUCCESS)
								{
									UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to instantiate CUDA graph instance %zu"), (size_t)Res);
								}
								Proxy->bUseCudaGraph = Res == CUDA_SUCCESS;
								GraphExecutionContext->bCudaGraphCaptured = Proxy->bUseCudaGraph;
							}
							else
							{
								Proxy->bUseCudaGraph = false;
								// This is a warning because we'll fall back to not using cuda graphs for this model
								UE_LOG(LogNNERuntimeTRT, Warning, TEXT("Failed to capture operations to CUDA graph %zu"), (size_t)Res);
							}
						}
						// Clean up the capture stream
						CudaAPI->cuStreamDestroy(CaptureStream);
					}
				}

				if (Proxy->bUseCudaGraph && GraphExecutionContext->bCudaGraphCaptured)
				{
					CUresult Res = CudaAPI->cuGraphLaunch(GraphExecutionContext->CudaGraphExec, Proxy->CudaStream);
					if (Res != CUDA_SUCCESS)
					{
						UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuGraphLaunch failed with error code %zu"), (size_t)Res);
					}
				}
				else
				{
					// Set the I/O tensor addresses and enqueue the model on the stream
					SetInputOutputAddresses();

					if (!Proxy->Context->enqueueV3(Proxy->CudaStream))
					{
						UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to enqueue model"));
					}
				}

				// Signal that inference is done
				CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS SignalParams = {};
				SignalParams.params.fence.value = SemaphoreCounter + 1;
				CudaAPI->cuSignalExternalSemaphoresAsync(&Proxy->CudaSemaphore, &SignalParams, 1, Proxy->CudaStream);

				if (FAILED(D3D12CommandQueue->Wait(Proxy->Fence, SemaphoreCounter + 1)))
				{
					UE_LOG(LogNNERuntimeTRT, Error, TEXT("Failed to signal D3D12 fence"));
				}
			}, false);
		});
	});

	SemaphoreCounter += 2u;

	// Copy output tensors from staging memory to output buffers
	for (auto& Mapping : OutputBuffersToCopy)
	{
		AddCopyBufferPass(RDGBuilder, Mapping.Dest, 0, Mapping.Src, 0, Mapping.Src->GetSize());
	}

	// Release tensor memory back to the pool after this RDG pass is complete
	RDGBuilder.AddPostExecuteCallback(
		[this]()
		{
			Proxy->MemoryPool->ReleaseAll();
		}
	);

	return EEnqueueRDGStatus::Ok;
}

bool FModelInstanceRDG::CreateSyncPrimitives()
{
	auto ScopedCudaContext = Environment->ActivateScopedCudaContext();
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = Environment->GetCudaAPI();

	// Create a fence and shared handle then import as an external cuda semaphore for synchronization between D3D queue and CUDA stream
	if (Proxy->Fence == nullptr)
	{
		HRESULT Res = GetID3D12DynamicRHI()->RHIGetDevice(0)->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&Proxy->Fence));
		if (FAILED(Res) || Proxy->Fence == nullptr)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("CreateFence failed with HRESULT %08X"), Res);
			return false;
		}
	}

	if (Proxy->FenceSharedHandle == nullptr)
	{
		HRESULT Res = GetID3D12DynamicRHI()->RHIGetDevice(0)->CreateSharedHandle(Proxy->Fence, nullptr, GENERIC_ALL, nullptr, &Proxy->FenceSharedHandle);
		if (FAILED(Res) || Proxy->FenceSharedHandle == nullptr)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("CreateSharedHandle for D3D12 Fence failed with HRESULT %08X"), Res);
			return false;
		}
	}

	if (Proxy->CudaSemaphore == nullptr)
	{
		CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC ExternalSemaphoreHandleDesc = {};
		ExternalSemaphoreHandleDesc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
		ExternalSemaphoreHandleDesc.handle.win32.handle = Proxy->FenceSharedHandle;

		CUresult Err = CudaAPI->cuImportExternalSemaphore(&Proxy->CudaSemaphore, &ExternalSemaphoreHandleDesc);
		if (Err != CUDA_SUCCESS)
		{
			UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuImportExternalSemaphore Error %zu"), (size_t)Err);
			return false;
		}
	}
	return true;
}

FModelRDG::FModelRDG(TSharedRef<Private::FEnvironment> InEnvironment, TSharedRef<NNE::FSharedModelData> InModelData) : Environment(InEnvironment), ModelData(InModelData)
{

}

TSharedPtr<UE::NNE::IModelInstanceRDG> FModelRDG::CreateModelInstanceRDG()
{
	TSharedPtr<FModelInstanceRDG> ModelInstance = MakeShared<FModelInstanceRDG>(Environment);
	if (!ModelInstance->Init(ModelData->GetView()))
	{
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("Cannot initialize model instance"));
		return {};
	}

	return ModelInstance;
}

} // namespace Private

} // namespace UE::NNERuntimeTRT
#endif // WITH_NNE_RUNTIME_TRT
