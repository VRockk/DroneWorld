#pragma once

#include "CoreMinimal.h"

namespace DroneFlight
{
	// The closing speed of a contact: how fast the drone is moving into the surface it hit, in cm/s.
	// It is the velocity projected onto the inward direction of the surface, so a head-on hit reads the
	// full approach speed while a glancing slide along a wall reads near zero even at high travel speed.
	// HitNormal is the outward surface normal (pointing back toward the drone); a contact moving away
	// from the surface clamps to zero. Plain data in, scalar out, so the crash decision is testable
	// without a live collision.
	DRONEWORLD_API float ComputeImpactSpeed(const FVector& Velocity, const FVector& HitNormal);

	// Whether a contact at this closing speed crashes the drone: true at or above the per-preset
	// threshold (cm/s), false below it, where the contact is a harmless bump. The single decision the
	// movement component consults on a blocking hit; respawn and scoring live elsewhere.
	DRONEWORLD_API bool ImpactCrashes(float ImpactSpeed, float CrashThreshold);
}
