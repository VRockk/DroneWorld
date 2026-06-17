#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightTypes.h"

namespace DroneFlight
{
	// A critically damped restoring term that drives Error to zero without overshoot: a proportional
	// pull of stiffness Stiffness (1/s^2) against the error plus a derivative term against Rate using
	// the critical damping coefficient 2*sqrt(Stiffness). The shared building block for every assist
	// that holds an angle or a position - self-leveling, hover position and attitude hold, loiter bank.
	// Pass a zero error to get pure damping (e.g. holding a heading by bleeding off yaw rate).
	inline float CriticalSpring(float Error, float Rate, float Stiffness)
	{
		const float Damping = 2.f * FMath::Sqrt(FMath::Max(Stiffness, 0.f));
		return -Error * Stiffness - Rate * Damping;
	}

	inline FVector CriticalSpring(const FVector& Error, const FVector& Rate, float Stiffness)
	{
		const float Damping = 2.f * FMath::Sqrt(FMath::Max(Stiffness, 0.f));
		return -Error * Stiffness - Rate * Damping;
	}

	// Angle-mode self-leveling: a body-axis corrective torque that rotates the drone back toward
	// level (zero roll, zero pitch) when the pilot releases the sticks. The correction on each of
	// roll and pitch is scaled down by how far the matching stick is deflected, so a held stick still
	// commands attitude and the drone only levels on release. Yaw is left to the pilot. In Acro the
	// correction is zero (the airframe holds whatever attitude it is left in). Applied on top of the
	// base force law; hover is handled separately by the model's hover force law. Plain data in, out.
	DRONEWORLD_API FVector ComputeLevelingTorque(
		EDroneAssistMode Mode,
		const FDroneControlIntent& Intent,
		const FDroneFlightState& State,
		float LevelingStrength);
}
