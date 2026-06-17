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
