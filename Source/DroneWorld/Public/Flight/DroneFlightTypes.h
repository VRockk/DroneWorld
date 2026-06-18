#pragma once

#include "CoreMinimal.h"
#include "DroneFlightTypes.generated.h"

// How much the flight assist helps the pilot. Acro is raw rate control with no self-leveling, so
// the drone holds whatever attitude it is left in and drifts. Angle self-levels to level when the
// sticks are released, so flying is approachable. Hover is not a value here: it is a separate toggle
// (bHoverEngaged) that overrides whichever of these is active, so the drone can return to its base
// mode the instant hover is released.
UENUM(BlueprintType)
enum class EDroneAssistMode : uint8
{
	Acro,
	Angle
};

// The device-independent pilot command. Every input source - gamepad, VR thumbsticks, keyboard,
// and the AI - produces one of these, and the movement component consumes it. Nothing wires a
// device directly to forces.
USTRUCT(BlueprintType)
struct FDroneControlIntent
{
	GENERATED_BODY()

	// Collective thrust demand, 0 (idle) .. 1 (full).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	float Throttle = 0.f;

	// Yaw stick demand, -1 .. 1 (positive = nose right, about body +Z).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	float Yaw = 0.f;

	// Pitch stick demand, -1 .. 1 (positive = nose up, about body +Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	float Pitch = 0.f;

	// Roll stick demand, -1 .. 1 (positive = roll right, about body +X).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	float Roll = 0.f;

	// Hover toggle: when set, the drone flies its Hover behavior (the Flight Model decides what that
	// means) instead of its base assist mode. The pilot flips this to park and look around, and clears
	// it to take the sticks again. Orthogonal to the Acro/Angle base mode rather than a value of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	bool bHoverEngaged = false;

	// Arm gate: the motors only respond to the sticks while this is set. A disarmed drone ignores
	// throttle and rotation entirely - the pilot arms to fly and disarms to cut the motors. Defaults to
	// disarmed so a freshly spawned drone sits inert until the pilot deliberately arms it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Intent")
	bool bArmed = false;
};

// The drone's instantaneous motion state, fed into the flight model and advanced by the integrator.
USTRUCT(BlueprintType)
struct FDroneFlightState
{
	GENERATED_BODY()

	// World-space linear velocity, cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|State")
	FVector Velocity = FVector::ZeroVector;

	// Body-axis angular velocity, deg/s (Roll about X, Pitch about Y, Yaw about Z).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|State")
	FVector AngularVelocity = FVector::ZeroVector;

	// World-space orientation of the drone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|State")
	FRotator Orientation = FRotator::ZeroRotator;

	// World-space location, cm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|State")
	FVector Location = FVector::ZeroVector;
};

// The per-preset configuration of the drone's camera gimbal: how far the controllable tilt may travel,
// where it starts, and how fast the pilot's hold-to-adjust slews it. It rides on the Preset alongside
// the other shared params, so each drone carries its own camera feel - a steep FPV uptilt on one, a
// gentler look-ahead on another. The mount is fixed to the airframe; only its pitch is controllable,
// so the camera banks and bobs with the drone. The tilt is driven by held D-pad presses that nudge the
// angle up or down rather than snapping to it, so it holds wherever the pilot releases it.
USTRUCT(BlueprintType)
struct FGimbalConfig
{
	GENERATED_BODY()

	// The lowest tilt the pilot can command, degrees. Negative points the camera down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal")
	float MinTiltDegrees = -45.f;

	// The highest tilt the pilot can command, degrees. Positive points the camera up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal")
	float MaxTiltDegrees = 30.f;

	// Where the tilt starts when the drone spawns, degrees. The classic FPV uptilt points the camera a
	// little off level so the drone flies looking ahead rather than at its feet; the pilot adjusts from
	// here and the tilt holds wherever they leave it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal")
	float DefaultTiltDegrees = 15.f;

	// How fast the tilt slews the instant the pilot presses the tilt key, degrees per second. A gentle
	// rate makes fine framing easy on a quick tap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal", meta = (ClampMin = "0.0"))
	float TiltSlewRate = 20.f;

	// How much the slew speeds up for each second the key is held, degrees per second squared, so a long
	// press sweeps the camera across its range quickly while a tap still nudges it precisely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal", meta = (ClampMin = "0.0"))
	float TiltSlewAcceleration = 60.f;

	// The fastest the tilt may slew however long the key is held, degrees per second, so the acceleration
	// tops out rather than running away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimbal", meta = (ClampMin = "0.0"))
	float TiltMaxSlewRate = 120.f;
};

// The per-preset shaping of raw stick travel into rotational Control Intent - the "rates" an RC pilot
// tunes so each drone responds with its own character. A centered deadzone removes stick drift near
// center; the expo curve bends the response so it is gentle around center and sharper toward the
// edges for fine control without giving up full deflection; and the rate scale sets overall
// sensitivity, so a punchy drone rotates harder than a docile one for the same stick.
USTRUCT(BlueprintType)
struct FDroneRates
{
	GENERATED_BODY()

	// Centered deadzone removing stick drift near center, as a fraction of travel. Input within this is
	// read as zero; travel beyond it is rescaled back to full range so the stick still reaches the edge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Deadzone = 0.05f;

	// Expo curve bend, 0 (linear) .. 1 (pure cubic). Higher expo softens the center for fine control
	// while preserving full deflection at the edge, so the stick feels calm in the middle and lively out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Expo = 0.3f;

	// Overall sensitivity multiplier on the shaped stick. A higher rate makes the drone rotate harder for
	// the same stick deflection, so two presets with different rates feel distinctly punchy or docile.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.0"))
	float RateScale = 1.f;
};

// The output of a flight model: the net linear force and the angular acceleration the integrator
// applies this step. Force is in mass-cm/s^2 (divide by mass for linear acceleration); Torque is a
// body-axis angular acceleration in deg/s^2 (Roll about X, Pitch about Y, Yaw about Z), so the
// integrator applies it without a separate moment-of-inertia term.
USTRUCT(BlueprintType)
struct FDroneForces
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Forces")
	FVector Force = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Forces")
	FVector Torque = FVector::ZeroVector;
};
