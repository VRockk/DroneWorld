#include "Flight/DroneCrash.h"

float DroneFlight::ComputeImpactSpeed(const FVector& Velocity, const FVector& HitNormal)
{
	// The hit normal points out of the surface, back toward the drone, so velocity directed into the
	// surface has a negative dot with it. Negate to get the inward closing speed, and clamp away the
	// negative case where the drone is already separating from the surface.
	const FVector Normal = HitNormal.GetSafeNormal();
	return FMath::Max(0.f, FVector::DotProduct(-Velocity, Normal));
}

bool DroneFlight::ImpactCrashes(float ImpactSpeed, float CrashThreshold)
{
	return ImpactSpeed >= CrashThreshold;
}
