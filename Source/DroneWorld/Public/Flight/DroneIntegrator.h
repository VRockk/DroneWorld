#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightTypes.h"

namespace DroneIntegrator
{
	// Advance a flight state by one semi-implicit Euler step: velocity is updated from the force
	// first, then position from the *updated* velocity (likewise angular velocity then orientation).
	// Force is in mass-cm/s^2 (divided by mass for acceleration); Torque is a body-axis angular
	// acceleration in deg/s^2. Collision response is not part of this pure seam.
	DRONEWORLD_API FDroneFlightState IntegrateStep(
		const FDroneFlightState& State,
		const FDroneForces& Forces,
		float Mass,
		float DeltaSeconds);
}
