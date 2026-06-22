#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/DroneFlightTypes.h"
#include "FixedWingFlightModel.generated.h"

// The fixed-wing tuning carried by a fixed-wing flight model. A plane lifts from forward airspeed
// over its wings rather than from collective thrust, stalls when that airspeed drops too low, and
// turns by banking so its lift vector pulls it around.
USTRUCT(BlueprintType)
struct FFixedWingFlightParams
{
	GENERATED_BODY()

	// Aircraft mass, kg. A heavier plane accelerates less for the same thrust and lift, since
	// acceleration is force over mass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.01"))
	float Mass = 8.f;

	// Thrust force at full throttle, in mass-cm/s^2 (a force, not an acceleration). Acts along the
	// body-forward axis; the acceleration it produces is this over Mass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float MaxThrust = 9600.f;

	// Minimum forward airspeed for the wing to make lift, cm/s. Below this the wing stalls.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float StallSpeed = 600.f;

	// Lift force per squared forward airspeed, in mass-cm/s^2 per (cm/s)^2. Lift force = LiftCoefficient
	// * airspeed^2 (a force); the acceleration it produces is this over Mass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float LiftCoefficient = 0.004f;

	// Largest lift force the wing can make, in mass-cm/s^2. The wing cannot make unbounded lift at
	// speed; past this the airflow would separate (stall), so lift saturates here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float MaxLift = 14400.f;

	// Linear drag force per unit velocity, in mass/s. Drag force = -DragCoefficient * Velocity,
	// independent of mass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float DragCoefficient = 4.8f;

	// Angular acceleration produced by a full roll/pitch/yaw stick, deg/s^2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float ControlAuthority = 120.f;

	// Angular rate damping, 1/s. Bleeds off body rotation so the aircraft stops turning when the sticks
	// are released instead of coasting; also sets the steady rate at full stick (ControlAuthority over
	// this).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ClampMin = "0.0"))
	float AngularDamping = 3.f;

	// Throttle held while loitering, 0..1. A plane cannot stop, so hover keeps it flying at this
	// cruise setting while it banks around the hold point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing|Loiter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LoiterThrottle = 0.5f;

	// Bank angle held while loitering, degrees. The aircraft banks to this angle for a believable
	// holding turn while it circles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing|Loiter", meta = (ClampMin = "0.0"))
	float LoiterBankAngle = 25.f;

	// Turn rate held while loitering, deg/s. The aircraft yaws at this rate so its nose comes around a
	// holding circle rather than flying straight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing|Loiter", meta = (ClampMin = "0.0"))
	float LoiterTurnRate = 30.f;

	// Attitude-hold stiffness while loitering, 1/s^2. Drives roll to the loiter bank, pitch to level,
	// and yaw to the turn rate; damping is derived from it for a critically damped hold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing|Loiter", meta = (ClampMin = "0.0"))
	float LoiterLevelStrength = 9.f;

	// Altitude-hold stiffness while loitering, 1/s^2. Holds the height the loiter was engaged at so the
	// aircraft circles level instead of stalling and dropping when slow. Critically damped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing|Loiter", meta = (ClampMin = "0.0"))
	float LoiterAltHoldGain = 4.f;
};

namespace DroneFlight
{
	// The pure fixed-wing force law: airspeed-driven lift along body-up that vanishes below stall,
	// throttle-driven thrust along body-forward, constant gravity, linear drag, and stick-proportional
	// angular acceleration. Plain data in, plain data out.
	DRONEWORLD_API FDroneForces ComputeFixedWingForces(
		const FDroneControlIntent& Intent,
		const FDroneFlightState& State,
		const FFixedWingFlightParams& Params);

	// The pure fixed-wing hover force law: a loiter circle, because a plane cannot stop on the spot. The
	// aircraft keeps flying at cruise throttle, banks to a held angle, yaws at a steady turn rate so its
	// nose comes around the circle, and holds the altitude it was engaged at. Only HoldLocation's height
	// is used (the circle's horizontal centre is wherever the plane drifts). Plain data in, plain out.
	DRONEWORLD_API FDroneForces ComputeFixedWingHoverForces(
		const FDroneControlIntent& Intent,
		const FDroneFlightState& State,
		const FVector& HoldLocation,
		const FFixedWingFlightParams& Params);
}

// A fixed-wing flight model: an aircraft that needs forward airspeed for lift, stalls when too slow,
// and turns by banking.
UCLASS()
class DRONEWORLD_API UFixedWingFlightModel : public UDroneFlightModel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FixedWing", meta = (ShowOnlyInnerProperties))
	FFixedWingFlightParams Params;

	virtual FDroneForces ComputeForces(const FDroneControlIntent& Intent, const FDroneFlightState& State) const override;
	virtual FDroneForces ComputeHoverForces(const FDroneControlIntent& Intent, const FDroneFlightState& State, const FVector& HoldLocation) const override;
	virtual float GetMass() const override { return Params.Mass; }
};
