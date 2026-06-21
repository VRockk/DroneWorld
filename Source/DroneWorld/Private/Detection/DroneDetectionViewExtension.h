#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include <atomic>

class FRHIGPUTextureReadback;

// Captures the final post-tonemap scene color (before Slate UI, so our boxes never feed back), downscales it
// to CaptureSize x CaptureSize in an RDG pass, and async-reads it to the CPU. All RHI work (the GPU readback
// Lock/Unlock) stays on the render thread; the detection subsystem pulls a finished CPU frame on the game
// thread via FetchFrame(). One capture in flight at a time (idle -> copy enqueued -> CPU frame ready -> idle).
class FDroneDetectionViewExtension : public FSceneViewExtensionBase
{
public:
	FDroneDetectionViewExtension(const FAutoRegister& AutoRegister);
	virtual ~FDroneDetectionViewExtension();

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	// Render thread: poll the in-flight readback and, when ready, Lock + copy it into the CPU frame buffer.
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

	void SetEnabled(bool bIn) { bEnabled.store(bIn); }

	// Game thread: if a finished CPU frame is available, move it into Out (CaptureSize*CaptureSize, BGRA) and return true.
	bool FetchFrame(TArray<FColor>& Out);

	static constexpr int32 CaptureSize = 880;

private:
	FScreenPassTexture OnPostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	std::atomic<bool> bEnabled{ false };
	std::atomic<int32> State{ 0 };          // 0 = idle (ready to capture), 1 = GPU copy enqueued / awaiting readback
	std::atomic<bool> bCpuReady{ false };
	FRHIGPUTextureReadback* Readback = nullptr;   // created/used/freed on the render thread only

	FCriticalSection FrameMutex;
	TArray<FColor> CpuFrame;                 // guarded by FrameMutex
};
