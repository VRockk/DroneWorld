#pragma once

#include "Templates/UniquePtr.h"
#ifdef WITH_NNE_RUNTIME_TRT

#include "CudaModule.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeRDG.h"
#include "NNERuntimeTRTModelData.h"
#include "NNETypes.h"
#include "RenderGraphResources.h"

namespace nvinfer1
{
	class IRuntime;
	class ICudaEngine;
	class IExecutionContext;
}

class ID3D12Fence;

namespace UE::NNERuntimeTRT
{

namespace Private
{

class FEnvironment;
class FMemoryPool;
class IAllocator;

class FModelInstanceGPU : public UE::NNE::IModelInstanceGPU
{
public:
	FModelInstanceGPU(TSharedRef<Private::FEnvironment> InEnvironment);
	virtual ~FModelInstanceGPU();

	bool Init(TConstArrayView64<uint8> ModelData);

	// IModelInstanceRunSync
	virtual TConstArrayView<UE::NNE::FTensorDesc> GetInputTensorDescs() const override;
	virtual TConstArrayView<UE::NNE::FTensorDesc> GetOutputTensorDescs() const override;
	virtual TConstArrayView<UE::NNE::FTensorShape> GetInputTensorShapes() const override;
	virtual TConstArrayView<UE::NNE::FTensorShape> GetOutputTensorShapes() const override;
	virtual ESetInputTensorShapesStatus SetInputTensorShapes(TConstArrayView<UE::NNE::FTensorShape> InInputShapes) override;
	virtual ERunSyncStatus RunSync(TConstArrayView<UE::NNE::FTensorBindingCPU> InInputTensors, TConstArrayView<UE::NNE::FTensorBindingCPU> InOutputTensors) override;

private:
	TSharedRef<Private::FEnvironment> Environment;

	nvinfer1::IRuntime* Runtime = nullptr;
	nvinfer1::IExecutionContext* Context = nullptr;
	nvinfer1::ICudaEngine* Engine = nullptr;
	CUstream CudaStream = nullptr;

	TArray<UE::NNE::FTensorDesc> InputTensorDescs;
	TArray<UE::NNE::FTensorDesc> OutputTensorDescs;

	TArray<UE::NNE::FTensorShape> InputTensorShapes;
	TArray<UE::NNE::FTensorShape> OutputTensorShapes;

	TUniquePtr<Private::IAllocator> Allocator;
	TUniquePtr<Private::FMemoryPool> MemoryPool;
};

class FModelGPU : public UE::NNE::IModelGPU
{
public:
	FModelGPU(TSharedRef<Private::FEnvironment> InEnvironment, TSharedRef<NNE::FSharedModelData> InModelData);

public:
	// IModelGPU Interface
	virtual TSharedPtr<UE::NNE::IModelInstanceGPU> CreateModelInstanceGPU() override;

private:
	TSharedRef<Private::FEnvironment> Environment;
	TSharedRef<NNE::FSharedModelData> ModelData;
};


class FModelInstanceRDG : public UE::NNE::IModelInstanceRDG
{
public:
	FModelInstanceRDG(TSharedRef<Private::FEnvironment> InEnvironment);
	virtual ~FModelInstanceRDG();

	bool Init(TConstArrayView64<uint8> ModelData);

	// IModelInstanceRDG
	virtual TConstArrayView<UE::NNE::FTensorDesc> GetInputTensorDescs() const override;
	virtual TConstArrayView<UE::NNE::FTensorDesc> GetOutputTensorDescs() const override;
	virtual TConstArrayView<UE::NNE::FTensorShape> GetInputTensorShapes() const override;
	virtual TConstArrayView<UE::NNE::FTensorShape> GetOutputTensorShapes() const override;
	
	virtual ESetInputTensorShapesStatus SetInputTensorShapes(TConstArrayView<UE::NNE::FTensorShape> InInputShapes) override;
	ESetInputTensorShapesStatus SetInputTensorShapes_RenderThread(TConstArrayView<NNE::FTensorShape> InInputShapes);

	virtual EEnqueueRDGStatus EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<UE::NNE::FTensorBindingRDG> InInputBindings, TConstArrayView<UE::NNE::FTensorBindingRDG> InOutputBindings) override;
protected:
	bool CreateSyncPrimitives();
private:
	TSharedRef<Private::FEnvironment> Environment;

	TArray<UE::NNE::FTensorDesc> InitialInputTensorDescs;
	TArray<UE::NNE::FTensorDesc> InitialOutputTensorDescs;

	TArray<UE::NNE::FTensorShape> InputTensorShapes;

	uint64 SemaphoreCounter = 0u;

	class FProxy; // Contains data written/read by RHI Thread and data initialized by Game/Render Thread
	TSharedPtr<FProxy> Proxy;
};

class FModelRDG : public UE::NNE::IModelRDG
{
public:
	FModelRDG(TSharedRef<Private::FEnvironment> InEnvironment, TSharedRef<NNE::FSharedModelData> InModelData);

public:
	// IModelRDG Interface
	virtual TSharedPtr<UE::NNE::IModelInstanceRDG> CreateModelInstanceRDG() override;

private:
	TSharedRef<Private::FEnvironment> Environment;
	TSharedRef<NNE::FSharedModelData> ModelData;
};

} // namespace Private

} // namespace UE::NNERuntimeTRT

#endif // WITH_NNE_RUNTIME_TRT