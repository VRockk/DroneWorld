#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightTypes.h"
#include "DroneImperfection.generated.h"

// The global wind the level/world supplies to every drone. It is one property shared by the whole
// level; the Imperfection Layer scales it per drone by the preset's susceptibility. Direction and base
// speed give the steady breeze; gust variance adds a slow coherent-noise ebb and flow on top, so open-
// air flying is a moving target rather than a constant push.
USTRUCT(BlueprintType)
struct FWorldWindParams
{
	GENERATED_BODY()

	// The direction the wind blows toward. Need not be normalized; only its direction is used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector Direction = FVector(1.f, 0.f, 0.f);

	// Steady wind force along Direction, mass-cm/s^2 before per-drone susceptibility.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0"))
	float BaseSpeed = 0.f;

	// Peak gust force added to or subtracted from the base, ebbing and flowing over time. Zero gives a
	// perfectly steady wind.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0"))
	float GustVariance = 0.f;

	// How quickly gusts come and go, Hz. Low values read as long, lazy gusts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0"))
	float GustFrequency = 0.15f;
};

// The per-preset tuning for the Imperfection Layer: the additive perturbation that keeps flight from
// ever being perfectly still or perfectly responsive. Shared across all flight models (it lives on the
// Preset and is applied uniformly), so a quad and a fixed-wing bob, drift, and feel the wind the same
// way, only tuned differently.
USTRUCT(BlueprintType)
struct FImperfectionParams
{
	GENERATED_BODY()

	// Hover bob: peak vertical force (mass-cm/s^2) of the slow oscillation applied while hovering, so a
	// parked drone breathes gently up and down instead of freezing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Bob", meta = (ClampMin = "0.0"))
	float HoverBobAmplitude = 400.f;

	// Hover bob frequency, Hz. Low values read as a slow, lazy bob.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Bob", meta = (ClampMin = "0.0"))
	float HoverBobFrequency = 0.5f;

	// Drift: peak horizontal force (mass-cm/s^2) of the smooth coherent-noise wander applied at all
	// times, so the drone is never perfectly still even in calm air.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Drift", meta = (ClampMin = "0.0"))
	float DriftAmplitude = 300.f;

	// Drift base frequency, Hz. The coherent noise sums a few slow sines around this rate, so lower
	// values wander more lazily.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Drift", meta = (ClampMin = "0.0"))
	float DriftFrequency = 0.2f;

	// Hover wander: peak horizontal distance (cm) the hold point drifts while hovering, so a parked
	// drone gently circles its spot instead of freezing on the setpoint. Uses the same coherent-noise
	// wander as the in-flight drift, applied to the hold point rather than as a force the hover spring
	// would only fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Drift", meta = (ClampMin = "0.0"))
	float DriftHoverRadius = 150.f;

	// Attitude wobble: peak coherent-noise torque (deg/s^2) applied to roll and pitch at all times, so
	// the airframe is never perfectly rigid and bobbles slightly even in a held hover. Yaw is left to the
	// pilot. Kept small; the leveling/hover attitude hold parks the steady offset near amplitude over
	// stiffness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Attitude", meta = (ClampMin = "0.0"))
	float AttitudeWobbleAmplitude = 12.f;

	// Attitude wobble frequency, Hz. The coherent noise sums a few slow sines around this rate; low
	// values read as a lazy bobble.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Attitude", meta = (ClampMin = "0.0"))
	float AttitudeWobbleFrequency = 0.7f;

	// Wind susceptibility: unitless gain on the world wind. A light drone uses a higher value so the
	// same world wind shoves it more than a heavy one does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Wind", meta = (ClampMin = "0.0"))
	float WindSusceptibility = 1.f;

	// Motor/throttle lag time constant, seconds. Larger spools thrust up more slowly; zero is instant.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Imperfection|Motor", meta = (ClampMin = "0.0"))
	float MotorLagTau = 0.15f;
};

namespace DroneFlight
{
	// The instantaneous world wind force: the steady breeze along Direction plus a coherent-noise gust
	// bounded by GustVariance. Pure of (params, seed, time): the same seed and time reproduce the same
	// wind, so the level's weather is deterministic. Returns a force vector the Imperfection Layer then
	// scales by each drone's susceptibility.
	DRONEWORLD_API FVector ComputeWorldWind(const FWorldWindParams& Wind, int32 Seed, float TimeSeconds);

	// The additive imperfection force the movement component adds after the Flight Model's ideal forces,
	// uniform across every model: a slow vertical hover bob (only while hovering), a smooth coherent-
	// noise horizontal drift, and the world wind scaled by the preset's susceptibility. Pure of
	// (params, seed, time, world wind): the same seed and time always yield the same perturbation, so a
	// drone drifts repeatably and two drones with different seeds wander independently. Torque is left
	// to the flight model; the layer perturbs the linear force only. When bGrounded the whole layer is
	// switched off: a drone resting on a surface is held by the ground, so it sits still rather than
	// drifting, catching the wind, or wobbling.
	DRONEWORLD_API FDroneForces ComputeImperfectionForces(
		const FImperfectionParams& Params,
		int32 Seed,
		float TimeSeconds,
		const FVector& WorldWind,
		bool bHovering,
		bool bGrounded = false);

	// The horizontal offset the hover hold point drifts to at this moment: the same coherent-noise
	// wander as the in-flight drift, scaled to DriftHoverRadius and returned as a position offset (cm)
	// rather than a force. Pure of (params, seed, time): the same seed and time reproduce the same
	// wander. The movement component adds this to the captured hold point while hovering, so the
	// position-hold spring tracks a gently moving target instead of fighting a drift force.
	DRONEWORLD_API FVector ComputeDriftOffset(const FImperfectionParams& Params, int32 Seed, float TimeSeconds);

	// First-order motor spool: advance the realized throttle toward the commanded one over the lag
	// time constant LagTau (seconds), so thrust ramps in rather than stepping. A real motor cannot
	// change RPM instantly, so a snapped stick still spools up across several ticks. Pure: the current
	// throttle in, the next throttle out. A non-positive LagTau means no lag (the command is realized
	// at once).
	DRONEWORLD_API float StepMotorLag(float CurrentThrust, float CommandedThrust, float LagTau, float DeltaSeconds);
}
