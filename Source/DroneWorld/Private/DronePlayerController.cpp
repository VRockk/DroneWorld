#include "DronePlayerController.h"
#include "PilotStation.h"
#include "View/DroneViewSelection.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "TimerManager.h"

void ADronePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Only the local pilot can have an HMD; querying it for a remote or AI-flown pawn is meaningless, so
	// the HMD check is gated on local control and the policy decides the rest.
	const bool bLocal = IsLocalController();
	const bool bHmd = bLocal && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();

	if (DroneView::SelectViewMode(bLocal, bHmd) == DroneView::EDroneViewMode::VrPilotStation)
	{
		SetupVrPilotStation(InPawn);
	}
	else if (bLocal)
	{
		// Flatscreen: the view target is the drone's onboard camera directly. No capture, no Pilot Station.
		SetViewTarget(InPawn);
	}
}

void ADronePlayerController::SetupVrPilotStation(APawn* InPawn)
{
	UWorld* World = GetWorld();
	if (!World || !InPawn || !PilotStationClass)
	{
		return;
	}

	// The drone owns the capture that feeds the screen; without it there is nothing to show.
	FeedCapture = InPawn->FindComponentByClass<USceneCaptureComponent2D>();
	if (!FeedCapture)
	{
		return;
	}

	// The world reaches the pilot through the Feed, not the onboard camera directly, so deactivate the
	// camera while the capture is in use. The capture shares the gimbal, so the Feed is still its view.
	OnboardCamera = InPawn->FindComponentByClass<UCameraComponent>();
	if (OnboardCamera)
	{
		OnboardCamera->SetActive(false);
	}

	// Build the Feed render target at the configured resolution and point both ends at it: the drone's
	// capture writes it, the Pilot Station screen reads it.
	FeedRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	FeedRenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
	FeedRenderTarget->InitAutoFormat(FMath::Max(FeedResolution.X, 1), FMath::Max(FeedResolution.Y, 1));
	FeedRenderTarget->UpdateResourceImmediate(true);
	FeedCapture->TextureTarget = FeedRenderTarget;

	// Capture every rendered frame, or on a fixed-rate timer when a capture rate is configured.
	if (FeedCaptureHz > 0.f)
	{
		FeedCapture->bCaptureEveryFrame = false;
		World->GetTimerManager().SetTimer(
			CaptureTimerHandle, this, &ADronePlayerController::CaptureFeed, 1.f / FeedCaptureHz, /*bLoop*/ true);
	}
	else
	{
		FeedCapture->bCaptureEveryFrame = true;
	}

	PilotStation = World->SpawnActor<APilotStation>(PilotStationClass, PilotStationTransform);
	if (PilotStation)
	{
		PilotStation->SetFeed(FeedRenderTarget);
		// Possession of the drone is unchanged; only the view target moves to the station in the Void.
		SetViewTarget(PilotStation);

		// Seat the pilot at the station: a Local (eye-level) tracking origin centers the HMD on the
		// station camera, so the screen sits at eye height rather than floor-relative, and a recenter
		// returns the view to the screen instead of drifting. Recenter once now so it starts dead ahead.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Local);
		UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.f);
	}
}

void ADronePlayerController::CaptureFeed()
{
	if (FeedCapture)
	{
		FeedCapture->CaptureScene();
	}
}

void ADronePlayerController::OnUnPossess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureTimerHandle);
	}

	// Stop the drone capturing and release the Feed before the station goes away, so a re-possess starts
	// from a clean view setup rather than a dangling render target.
	if (FeedCapture)
	{
		FeedCapture->bCaptureEveryFrame = false;
		FeedCapture->TextureTarget = nullptr;
		FeedCapture = nullptr;
	}
	if (OnboardCamera)
	{
		// Hand the view back to the onboard camera so the pawn can be flown flatscreen again.
		OnboardCamera->SetActive(true);
		OnboardCamera = nullptr;
	}
	if (PilotStation)
	{
		PilotStation->Destroy();
		PilotStation = nullptr;
	}
	FeedRenderTarget = nullptr;

	Super::OnUnPossess();
}
