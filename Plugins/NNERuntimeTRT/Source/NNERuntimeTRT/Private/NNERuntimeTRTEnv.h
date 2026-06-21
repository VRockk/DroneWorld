#pragma once

#ifdef WITH_NNE_RUNTIME_TRT

#include "CudaWrapper.h"
#include "HAL/CriticalSection.h"
#include "Templates/UniquePtr.h"

namespace UE::NNERuntimeTRT::Private
{

class FScopedCudaContext {
public:
	~FScopedCudaContext()
	{
		if (CudaAPI)
		{
			CudaAPI->cuCtxPopCurrent(nullptr);
		}
	}
protected:
	FScopedCudaContext(const CUDA_DRIVER_API_FUNCTION_LIST* InCudaAPI, CUcontext InCudaContext)
		: CudaAPI(InCudaAPI)
	{
		if (CudaAPI)
		{
			CudaAPI->cuCtxPushCurrent(InCudaContext);
		}
	}
	friend class FEnvironment;

	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = nullptr;
};

class FEnvironment
{
public:
	FEnvironment(const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI);
	~FEnvironment();

	const CUDA_DRIVER_API_FUNCTION_LIST* GetCudaAPI() const { return CudaApi; }

	CUcontext GetCudaContext() const;

	FScopedCudaContext ActivateScopedCudaContext();

private:
	bool CreateCudaContext() const;

	const CUDA_DRIVER_API_FUNCTION_LIST* CudaApi = nullptr;

	class FCudaContextHandle;
	mutable TUniquePtr<FCudaContextHandle> CudaContext;
	mutable FCriticalSection CriticalSection;
};

} // namespace UE::NNERuntimeTRT::Private

#endif // WITH_NNE_RUNTIME_TRT