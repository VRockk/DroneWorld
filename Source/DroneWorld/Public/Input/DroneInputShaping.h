#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightTypes.h"

namespace DroneInput
{
	// Shape a raw stick axis in [-1, 1]: a centered deadzone removes drift near center, then the
	// remaining travel is rescaled to [0, 1] and bent by an expo curve for finer control near center.
	// out = (1 - Expo) * x + Expo * x^3, with the sign of Raw preserved. Deadzone and Expo are in [0, 1].
	DRONEWORLD_API float ShapeAxis(float Raw, float Deadzone, float Expo);

	// Map RC "Mode 2" gamepad sticks to Control Intent: the left stick is throttle (Y) and yaw (X),
	// the right stick is pitch (Y) and roll (X). The throttle stick rests at zero and is pushed up to
	// add collective (upper half of travel maps to [0, 1]), so releasing it means no thrust. Rotational
	// axes pass through unchanged.
	DRONEWORLD_API FDroneControlIntent MapMode2(float LeftX, float LeftY, float RightX, float RightY);
}
