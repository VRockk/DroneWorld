#include "Flight/DroneFriction.h"

FVector DroneFlight::ApplySurfaceFriction(const FVector& Velocity, const FVector& SurfaceNormal, float FrictionDeceleration, float DeltaSeconds)
{
	const FVector Normal = SurfaceNormal.GetSafeNormal();
	if (FrictionDeceleration <= 0.f || Normal.IsNearlyZero())
	{
		return Velocity;
	}

	// Split the velocity into the part pressing into the surface (kept, so friction never makes the drone
	// sink or float) and the part sliding across it (the only part friction acts on).
	const FVector NormalVelocity = FVector::DotProduct(Velocity, Normal) * Normal;
	const FVector TangentVelocity = Velocity - NormalVelocity;

	const float TangentSpeed = TangentVelocity.Size();
	if (TangentSpeed <= KINDA_SMALL_NUMBER)
	{
		return Velocity;
	}

	// Constant deceleration scrubs the slide to a dead stop in finite time, clamped at zero so an
	// over-strong step settles the drone rather than flinging it backwards.
	const float ScrubbedSpeed = FMath::Max(0.f, TangentSpeed - FrictionDeceleration * DeltaSeconds);
	const FVector ScrubbedTangent = TangentVelocity * (ScrubbedSpeed / TangentSpeed);

	return NormalVelocity + ScrubbedTangent;
}
