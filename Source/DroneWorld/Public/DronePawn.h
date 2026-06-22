#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Flight/DroneFlightTypes.h"
#include "Flight/DroneMotorAudio.h"
#include "DronePawn.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UAudioComponent;
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

	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;

	// Advance the gimbal tilt one step: integrate the held direction (-1 down, +1 up, 0 idle) over
	// DeltaSeconds at a slew rate that ramps with HeldSeconds, so the longer the pilot holds the tilt key
	// the faster the camera pitches, clamped to the configured range. Returns the new tilt (degrees);
	// HeldSeconds is the time the key has been held so far this press. Pure of (config, state, input, dt).
	static float StepGimbalTilt(const FGimbalConfig& Config, float CurrentDegrees, float Direction, float HeldSeconds, float DeltaSeconds);

	// How far the rotor blades turn this frame (degrees): the constant spin rate over DeltaSeconds while
	// armed, and nothing while disarmed, so the blades spin only when the motors are live. The rate is held
	// constant rather than scaled by thrust - the difference is not readable on a spinning blade in game.
	// Pure of (armed, rate, dt).
	static float StepRotorSpin(bool bArmed, float SpinRateDegreesPerSecond, float DeltaSeconds);

protected:
	// Swept collision body and pawn root.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USphereComponent> CollisionRoot;

	// Visible drone body; the mesh asset is assigned in Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	// The blades spin while armed. They are not fixed components on the pawn: a drone Blueprint adds however
	// many blade mesh components it needs - four for a quad, one propeller for a fixed-wing, any count - and
	// tags each with RotorMeshTag, which BeginPlay gathers and the tick spins. Nothing here assumes a rotor
	// count, so the same pawn serves every airframe.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rotors")
	FName RotorMeshTag = TEXT("Rotor");

	// The local axis each tagged blade spins around. Default up suits a quad's rotors lying flat; a
	// fixed-wing propeller stands on its nose, so set this to its forward axis in that Blueprint. Applied in
	// each blade's local space, so a blade mounted at any angle still spins about the airframe-relative axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rotors")
	FVector RotorSpinAxis = FVector::UpVector;

	// How fast the blades spin while armed, degrees per second. A constant blur rather than a true rotor
	// speed - tuned per Blueprint for the look of that drone, not its thrust.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rotors")
	float RotorSpinRateDegreesPerSecond = 1800.f;

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

	// The four looping rotor sounds, one per corner, laid out as seen from above with the drone facing
	// +X. Each is voiced from its own rotor's level so banking swings the sound across the airframe and
	// spatialized audio pans it between the corners. Assign the looping motor sound and attenuation on
	// each in Blueprint; the corner spacing is the drone's own size, so set it per pawn.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Audio")
	TObjectPtr<UAudioComponent> RotorAudioFrontLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Audio")
	TObjectPtr<UAudioComponent> RotorAudioFrontRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Audio")
	TObjectPtr<UAudioComponent> RotorAudioRearLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Audio")
	TObjectPtr<UAudioComponent> RotorAudioRearRight;

	// This drone's motor voice - idle/full volume and pitch, and how hard banking swings the sound
	// between rotors. Lives on the pawn beside the rotor sounds so each drone Blueprint carries its own
	// motor character without touching the flight-tuning Preset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Audio")
	FMotorAudioParams MotorAudio;

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

	// Arm toggle (digital button): each press arms or disarms the drone. The motors only respond to the
	// sticks while armed, so the pilot arms to fly and disarms to cut them. Same bind setup as the other
	// actions; assign an Input Action here and map a key to it, or leave unset to keep the drone disarmed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputAction> ArmToggleAction;

	// Camera gimbal tilt (Axis1D): the held direction that pitches the onboard camera, +1 up and -1 down.
	// Map it to the D-pad up/down; holding nudges the tilt within the preset's range and it speeds up the
	// longer it is held. Assign an Input Action here; leave unset to keep the camera at the default tilt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input")
	TObjectPtr<UInputAction> GimbalTiltAction;

private:
	void OnThrottleYaw(const FInputActionValue& Value);
	void OnPitchRoll(const FInputActionValue& Value);
	void OnToggleHover(const FInputActionValue& Value);
	void OnToggleArm(const FInputActionValue& Value);
	void OnGimbalTilt(const FInputActionValue& Value);
	void PushControlIntent();

	// Disarm the drone when the movement component reports a crash. A crash already cuts the motors; this
	// latches that into the arm state so the drone stays disarmed through the respawn and the pilot must
	// deliberately re-arm before a recovered drone flies again, rather than it leaping back to life.
	UFUNCTION()
	void OnDroneCrashed(APawn* CrashedPawn);

	// Advance the gimbal tilt for this frame and point the mount at it, integrating the held direction
	// over DeltaSeconds. Moves the mount that carries both the onboard camera and the Feed capture, so the
	// tilt shows up identically on the flatscreen view and the VR Feed.
	void UpdateGimbal(float DeltaSeconds);

	// Voice the four rotor sounds for this frame: mix the realized (motor-lagged) throttle across the
	// corners by the current attitude demand, then set each rotor's volume and pitch from its level,
	// silenced while disarmed. Reads the live motor state from the movement component, so the sound spools
	// up with the thrust and banking swings it between the corners.
	void UpdateMotorAudio();

	// Collect the blade meshes once at BeginPlay: every static mesh component the Blueprint tagged with
	// RotorMeshTag. The set is fixed for the pawn's life, so it is gathered once rather than searched each tick.
	void GatherRotorMeshes();

	// Spin the gathered blades this frame: turn each one about RotorSpinAxis by the armed spin step, so they
	// blur while the motors are live and sit still once disarmed.
	void UpdateRotorSpin(float DeltaSeconds);

	// The blade meshes tagged with RotorMeshTag, gathered at BeginPlay and spun each tick while armed.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RotorMeshes;

	// Latest raw stick positions, combined into Control Intent whenever either stick changes.
	FVector2D ThrottleYawStick = FVector2D::ZeroVector;
	FVector2D PitchRollStick = FVector2D::ZeroVector;

	// Latched hover state, flipped by each press of the hover toggle and folded into Control Intent.
	bool bHoverEngaged = false;

	// Latched arm state, flipped by each press of the arm toggle and folded into Control Intent. Starts
	// disarmed so a freshly possessed drone sits inert until the pilot deliberately arms it.
	bool bArmed = false;

	// The intent last pushed to the movement component, kept so the motor-audio mix can read the live
	// attitude demand (the shaped roll/pitch/yaw) and arm state without recomputing them.
	FDroneControlIntent LastIntent;

	// The current gimbal tilt (pitch, degrees), held wherever the pilot leaves it. Seeded to the preset's
	// default tilt on BeginPlay and slewed by the held tilt direction each tick.
	float GimbalTiltDegrees = 0.f;

	// The held tilt direction from input: +1 up, -1 down, 0 idle. Set by the D-pad action, integrated by
	// the tick rather than applied directly, so a held button sweeps the tilt instead of snapping it.
	float GimbalTiltDirection = 0.f;

	// How long the current tilt direction has been held, seconds. Drives the accelerating slew and resets
	// when the key is released or the direction reverses.
	float GimbalHeldSeconds = 0.f;

	// The tilt direction applied last tick, so a reversal (up to down without releasing) restarts the
	// hold timer and the slew accelerates from the base rate again.
	float GimbalLastTiltDirection = 0.f;
};
