#include "NNERuntimeTRTEnv.h"

#include "Misc/ScopeLock.h"
#include "NNERuntimeTRTLog.h"

#ifdef WITH_NNE_RUNTIME_TRT

namespace UE::NNERuntimeTRT::Private
{

class FEnvironment::FCudaContextHandle
{
public:
	FCudaContextHandle(const CUDA_DRIVER_API_FUNCTION_LIST* InCudaAPI, CUcontext Context)
		: CudaAPI(InCudaAPI), CudaContext(Context)
	{

	}

	~FCudaContextHandle()
	{
		if (CudaAPI && CudaContext)
		{
			CudaAPI->cuCtxDestroy(CudaContext);
		}
	}

	CUcontext Get() const { return CudaContext; }

private:
	const CUDA_DRIVER_API_FUNCTION_LIST* CudaAPI = nullptr;
	CUcontext CudaContext{};
};

FEnvironment::FEnvironment(const CUDA_DRIVER_API_FUNCTION_LIST* InCudaApi) : CudaApi(InCudaApi)
{
	checkf(CudaApi, TEXT("Cuda API function list cannot be null"));
}

FEnvironment::~FEnvironment() = default;

CUcontext FEnvironment::GetCudaContext() const
{
	FScopeLock ScopeLock(&CriticalSection);

	if (!CudaContext.IsValid())
	{
		if (!CreateCudaContext())
		{
			return nullptr;
		}
	}

	return CudaContext->Get();
}

FScopedCudaContext FEnvironment::ActivateScopedCudaContext()
{
	return FScopedCudaContext(CudaApi, GetCudaContext());
}

bool FEnvironment::CreateCudaContext() const
{
#if 0 // TODO: CiG (CUDA Interop Graphics) requires CUctxCreateParams/CUctxCigParam types not in engine's cuda.h
	CUctxCreateParams ctxCreateParams;
	CUctxCigParam ctxCigParams;

	int d3dCigSupported = 0;
	// TODO: CiG + TRTRTX has some issues atm, disable CiG for now
	// CudaAPI->cuDeviceGetAttribute(&d3dCigSupported, CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED, CudaDevice);
	if (d3dCigSupported == 1 && GDynamicRHI != nullptr) {
		if (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D12)
		{
			ctxCreateParams = { nullptr, 0, nullptr };
		}
		else
		{
			auto RHI = static_cast<ID3D12DynamicRHI*>(GDynamicRHI);
			auto commandQueue = RHI->RHIGetCommandQueue();
			ctxCigParams = { CIG_DATA_TYPE_D3D12_COMMAND_QUEUE, (void*)commandQueue };
			ctxCreateParams = { nullptr, 0, &ctxCigParams };
		}
	}
	else {
		ctxCreateParams = { nullptr, 0, nullptr };
	}
#endif

	// Create the CUDA context
	if (CudaApi->cuInit(0) != CUDA_SUCCESS) {
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuInit Error"));
		return false;
	}

	CUdevice CudaDevice;
	CudaApi->cuDeviceGet(&CudaDevice, 0);

	CUcontext CudaContextPtr = nullptr;
	auto CudaError = CudaApi->cuCtxCreate(&CudaContextPtr, 0, CudaDevice);
	if (CudaError != CUDA_SUCCESS) {
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuCtxCreate Error %zu"), (size_t)CudaError);
		return false;
	}

	CudaContext = MakeUnique<FCudaContextHandle>(CudaApi, CudaContextPtr);

	// Have to restore the previous CUDA context (if any) since cuCtxCreate makes the new context current
	CudaError = CudaApi->cuCtxPopCurrent(nullptr);
	if (CudaError != CUDA_SUCCESS) {
		UE_LOG(LogNNERuntimeTRT, Error, TEXT("cuCtxPopCurrent Error %zu"), (size_t)CudaError);
		return false;
	}

	return true;
}

} // namespace UE::NNERuntimeTRT::Private

#endif // WITH_NNE_RUNTIME_TRT