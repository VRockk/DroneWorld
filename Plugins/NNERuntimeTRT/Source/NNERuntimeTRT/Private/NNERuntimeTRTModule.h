#pragma once

#include "Modules/ModuleInterface.h"
#include "NNERuntimeTRTLog.h"

#ifdef WITH_NNE_RUNTIME_TRT
#include "NNERuntimeTRT.h"
#include "UObject/WeakObjectPtrTemplates.h"
#endif // WITH_NNE_RUNTIME_TRT

namespace UE::NNERuntimeTRT::Private
{
	class FEnvironment;
}

class FNNERuntimeTRTModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#ifdef WITH_NNE_RUNTIME_TRT
private:
	void RegisterRuntime();

	TArray<void*> DllHandles;
	TWeakObjectPtr<UNNERuntimeTRT> NNERuntimeTRT;
	TSharedPtr<UE::NNERuntimeTRT::Private::FEnvironment> Environment;
	bool bCUDAAvailable = false;
#endif // WITH_NNE_RUNTIME_TRT
};
