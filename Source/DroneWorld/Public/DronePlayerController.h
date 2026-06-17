#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DronePlayerController.generated.h"

class APilotStation;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// The VR orchestration controller. It possesses the drone pawn (input -> movement) like any controller,
// but on possession it picks the view setup: for the local pilot wearing an HMD it stands up
// a Pilot Station in the Void, points the drone's onboard capture at a Feed render target shown on the
// station screen, and makes the station the view target while possession of the drone is unchanged.
// Everyone else - a flatscreen pilot, and any pawn flown without an HMD - views the drone's onboard
// camera directly, so the Void and the Feed capture are never paid for them.
UCLASS()
class DRONEWORLD_API ADronePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	// The Pilot Station spawned for a local VR pilot. Assign a Blueprint subclass so its screen mesh,
	// material, and OSD are authored in the editor; left unset, no station is spawned.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drone|VR")
	TSubclassOf<APilotStation> PilotStationClass;

	// Where the Pilot Station is spawned. It should sit inside the stable, nearly-empty Void away from the
	// flyable world, since this transform is what the HMD actually renders around the pilot.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drone|VR")
	FTransform PilotStationTransform;

	// Feed render-target resolution, a tuning knob trading Feed sharpness against GPU cost.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drone|VR", meta = (ClampMin = "1"))
	FIntPoint FeedResolution = FIntPoint(1280, 720);

	// How often the Feed is captured, in hertz. Zero captures every rendered frame; a positive rate
	// captures on a timer, trading Feed smoothness against the cost of re-rendering the world.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drone|VR", meta = (ClampMin = "0.0"))
	float FeedCaptureHz = 0.f;

private:
	// Stand up the VR view: build the Feed render target, wire the drone's capture to it, spawn the Pilot
	// Station showing that Feed, and make the station the view target.
	void SetupVrPilotStation(APawn* InPawn);

	// Re-render the Feed once; driven on a timer when a capture rate is configured.
	void CaptureFeed();

	// The drone capture writing the Feed, held so it can be reset when possession ends.
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> FeedCapture;

	// The onboard camera, deactivated while the Feed drives the view, held so it is reactivated when
	// possession ends and the pawn can be flown flatscreen again.
	UPROPERTY()
	TObjectPtr<UCameraComponent> OnboardCamera;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> FeedRenderTarget;

	UPROPERTY()
	TObjectPtr<APilotStation> PilotStation;

	FTimerHandle CaptureTimerHandle;
};
