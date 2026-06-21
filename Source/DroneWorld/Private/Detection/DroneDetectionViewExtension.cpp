#include "Detection/DroneDetectionViewExtension.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"

FDroneDetectionViewExtension::FDroneDetectionViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

FDroneDetectionViewExtension::~FDroneDetectionViewExtension()
{
	if (Readback)
	{
		FRHIGPUTextureReadback* R = Readback;
		Readback = nullptr;
		ENQUEUE_RENDER_COMMAND(DeleteDroneReadback)([R](FRHICommandListImmediate&) { delete R; });
	}
}

void FDroneDetectionViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (bEnabled.load() && Pass == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FDroneDetectionViewExtension::OnPostProcess_RenderThread));
	}
}

FScreenPassTexture FDroneDetectionViewExtension::OnPostProcess_RenderThread(FRDGBuilder& GraphBuilder,
	const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	// Resolve the scene color (handles OverrideOutput so the post chain continues correctly).
	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice, FScreenPassTexture(Inputs.OverrideOutput));

	// Enqueue a capture only when no readback is in flight (idle -> copy enqueued).
	int32 Expected = 0;
	if (bEnabled.load() && SceneColor.IsValid() && State.compare_exchange_strong(Expected, 1))
	{
		if (!Readback)
		{
			Readback = new FRHIGPUTextureReadback(TEXT("DroneDetectReadback"));
		}

		const FIntPoint DestExtent(CaptureSize, CaptureSize);
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(DestExtent, PF_B8G8R8A8, FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Dest = GraphBuilder.CreateTexture(Desc, TEXT("DroneDetectCapture"));

		// Stretch the full view into the square capture (matches the model's square-resize; normalized coords
		// then map 1:1 to the viewport).
		FScreenPassRenderTarget DestRT(FScreenPassTexture(Dest, FIntRect(0, 0, CaptureSize, CaptureSize)),
			ERenderTargetLoadAction::ENoAction);
		AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), SceneColor, DestRT);

		AddEnqueueCopyPass(GraphBuilder, Readback, Dest);
	}

	return SceneColor;
}

void FDroneDetectionViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Render thread: when the in-flight copy has landed, Lock it here (RHI requires the render thread) and
	// copy into the CPU frame for the game thread to consume.
	if (State.load() != 1 || !Readback || !Readback->IsReady())
	{
		return;
	}

	int32 RowPitchInPixels = 0;
	const FColor* Src = reinterpret_cast<const FColor*>(Readback->Lock(RowPitchInPixels));
	if (Src)
	{
		{
			FScopeLock Lock(&FrameMutex);
			CpuFrame.SetNumUninitialized(CaptureSize * CaptureSize);
			for (int32 y = 0; y < CaptureSize; ++y)
			{
				FMemory::Memcpy(&CpuFrame[y * CaptureSize], &Src[y * RowPitchInPixels], CaptureSize * sizeof(FColor));
			}
		}
		Readback->Unlock();
		bCpuReady.store(true);
	}

	State.store(0);   // ready for the next capture
}

bool FDroneDetectionViewExtension::FetchFrame(TArray<FColor>& Out)
{
	if (!bCpuReady.load())
	{
		return false;
	}
	FScopeLock Lock(&FrameMutex);
	if (CpuFrame.Num() < CaptureSize * CaptureSize)
	{
		bCpuReady.store(false);
		return false;
	}
	Out = MoveTemp(CpuFrame);
	bCpuReady.store(false);
	return true;
}
