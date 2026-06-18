#pragma once

#include "CoreMinimal.h"

namespace DroneFlight
{
	// Scrub the velocity tangent to a contact surface to simulate dry friction while the drone rests on
	// it. The component along SurfaceNormal (pressing into or lifting off the surface) is left untouched;
	// only the component sliding across the surface is bled, by a constant FrictionDeceleration (cm/s^2)
	// over DeltaSeconds, clamped so it settles to a dead stop rather than reversing. A disarmed or crashed
	// drone has no motors to hold it, so this is what stops it gliding along the ground forever. Pure of
	// (velocity, normal, deceleration, dt): plain vectors in, the scrubbed velocity out, so it is testable
	// without a live collision. A zero or non-positive deceleration leaves the velocity unchanged.
	DRONEWORLD_API FVector ApplySurfaceFriction(const FVector& Velocity, const FVector& SurfaceNormal, float FrictionDeceleration, float DeltaSeconds);
}
