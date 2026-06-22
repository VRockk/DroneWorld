#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/DroneFlightTypes.h"
#include "QuadFlightModel.generated.h"

// The quadcopter-specific tuning carried by a quad flight model. A multirotor lifts from collective
// thrust along its body-up axis and can hover when that thrust overcomes gravity.
USTRUCT(BlueprintType)
struct FQuadFlightParams
{
	GENERATED_BODY()

	// Drone mass, kg. A heavier drone accelerates less for the same thrust and needs more throttle to
	// hover, since acceleration is force over mass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ClampMin = "0.01"))
	float Mass = 5.f;

	// Thrust force at full collective, in mass-cm/s^2 (a force, not an acceleration). The lift it
	// produces is this over Mass, so it must exceed the drone's weight (Mass * GravityAccel) to hover.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ClampMin = "0.0"))
	float MaxThrust = 10000.f;

	// Linear drag force per unit velocity, in mass/s. Drag force = -DragCoefficient * Velocity, so a
	// heavier drone coasts longer for the same drag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ClampMin = "0.0"))
	float DragCoefficient = 2.5f;

	// Angular acceleration produced by a full yaw/pitch/roll stick, deg/s^2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ClampMin = "0.0"))
	float ControlAuthority = 180.f;

	// Angular rate damping, 1/s. Bleeds off body rotation so the drone stops turning when the sticks
	// are released instead of coasting; also sets the steady rate at full stick (ControlAuthority over
	// this). Without it, yaw in particular would spin forever once started.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ClampMin = "0.0"))
	float AngularDamping = 3.f;

	// Position-hold stiffness while hovering, 1/s^2. Restoring acceleration is this gain times the
	// distance from the held point; damping is derived from it for a critically damped, non-oscillating
	// return. Higher values park more tightly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad|Hover", meta = (ClampMin = "0.0"))
	float HoverPositionGain = 4.f;

	// Attitude-hold stiffness while hovering, 1/s^2. Drives roll and pitch back to level (and damps
	// yaw) so the drone sits flat in the hover. Damping is derived from it for a critically damped hold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad|Hover", meta = (ClampMin = "0.0"))
	float HoverLevelStrength = 9.f;
};

namespace DroneFlight
{
	// The pure quad force law: collective thrust along body-up, constant gravity, linear drag, and
	// stick-proportional angular acceleration. Plain data in, plain data out.
	DRONEWORLD_API FDroneForces ComputeQuadForces(
		const FDroneControlIntent& Intent,
		const FDroneFlightState& State,
		const FQuadFlightParams& Params);

	// The pure quad hover force law: gravity compensation plus a critically damped pull back toward
	// HoldLocation (horizontal position and altitude hold) and toward level attitude. A quad can stop
	// in place, so hover parks it on the held point. Plain data in, plain data out.
	DRONEWORLD_API FDroneForces ComputeQuadHoverForces(
		const FDroneControlIntent& Intent,
		const FDroneFlightState& State,
		const FVector& HoldLocation,
		const FQuadFlightParams& Params);
}

// A quadcopter flight model: a multirotor that lifts from collective thrust and can hover.
UCLASS()
class DRONEWORLD_API UQuadFlightModel : public UDroneFlightModel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad", meta = (ShowOnlyInnerProperties))
	FQuadFlightParams Params;

	virtual FDroneForces ComputeForces(const FDroneControlIntent& Intent, const FDroneFlightState& State) const override;
	virtual FDroneForces ComputeHoverForces(const FDroneControlIntent& Intent, const FDroneFlightState& State, const FVector& HoldLocation) const override;
	virtual float GetMass() const override { return Params.Mass; }
};
