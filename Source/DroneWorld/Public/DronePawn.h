#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DronePawn.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UDroneMovementComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

// The drone: a single pawn flown by either a human player or the AI. It owns the kinematic movement
// component, the onboard camera on a gimbal mount, and the arm/disarm state, and binds Enhanced
// Input to the device-independent Control Intent. The same pawn serves the flatscreen player, the
// VR player, and the AI - none of the input wiring is duplicated per controller.
UCLASS()
class DRONEWORLD_API ADronePawn : public APawn
{
	GENERATED_BODY()

public:
	ADronePawn();

	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	// Swept collision body and pawn root.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USphereComponent> CollisionRoot;

	// Visible drone body; the mesh asset is assigned in Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	// Pivot the onboard camera tilts on; the controllable gimbal mount.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USceneComponent> GimbalMount;

	// The camera physically mounted on the drone - the pilot's view in flatscreen, and the source of
	// the VR Feed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UCameraComponent> OnboardCamera;

	// Captures the onboard camera's view to a render target so it can be shown as the Feed on the VR
	// Pilot Station's screen. Shares the gimbal mount with the onboard camera, so it sees what the pilot
	// flies by. Idle until a controller points it at a render target; flatscreen flight never captures.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USceneCaptureComponent2D> FeedCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDroneMovementComponent> DroneMovement;

	// Enhanced Input assets, assigned in Blueprint. The mapping context carries both the gamepad
	// RC "Mode 2" bindings and the keyboard fallback onto these two stick actions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputMappingContext> FlightMappingContext;

	// Left stick (Axis2D): X = yaw, Y = throttle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputAction> ThrottleYawAction;

	// Right stick (Axis2D): X = roll, Y = pitch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputAction> PitchRollAction;

	// Hover toggle (digital button): each press flips the drone between its base assist mode and
	// Hover. Assign an Input Action here and map a key to it in the mapping context; leave unset to
	// disable the toggle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputAction> HoverToggleAction;

	// Per-axis input shaping applied to the rotational sticks before they become Control Intent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickDeadzone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickExpo = 0.3f;

private:
	void OnThrottleYaw(const FInputActionValue& Value);
	void OnPitchRoll(const FInputActionValue& Value);
	void OnToggleHover(const FInputActionValue& Value);
	void PushControlIntent();

	// Latest raw stick positions, combined into Control Intent whenever either stick changes.
	FVector2D ThrottleYawStick = FVector2D::ZeroVector;
	FVector2D PitchRollStick = FVector2D::ZeroVector;

	// Latched hover state, flipped by each press of the hover toggle and folded into Control Intent.
	bool bHoverEngaged = false;
};
