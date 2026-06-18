#include "Flight/DroneAssist.h"

FVector DroneFlight::ComputeLevelingTorque(
	EDroneAssistMode Mode,
	const FDroneControlIntent& Intent,
	const FDroneFlightState& State,
	float LevelingStrength)
{
	// Only Angle mode self-levels; Acro holds whatever attitude the drone is left in.
	if (Mode != EDroneAssistMode::Angle)
	{
		return FVector::ZeroVector;
	}

	// Drive roll and pitch back toward zero with a critically damped corrective torque, so the drone
	// settles to level without overshooting and ringing.
	const float RollTorque = CriticalSpring(State.Orientation.Roll, State.AngularVelocity.X, LevelingStrength);
	const float PitchTorque = CriticalSpring(State.Orientation.Pitch, State.AngularVelocity.Y, LevelingStrength);

	// A deflected stick suppresses leveling on its own axis, so a held stick still commands attitude
	// and the drone only returns to level once the pilot lets go. Yaw is always the pilot's.
	const float RollRelease = 1.f - FMath::Min(FMath::Abs(Intent.Roll), 1.f);
	const float PitchRelease = 1.f - FMath::Min(FMath::Abs(Intent.Pitch), 1.f);

	return FVector(RollTorque * RollRelease, PitchTorque * PitchRelease, 0.f);
}

FVector DroneFlight::ComputeGroundSettleTorque(
	const FRotator& Orientation,
	const FVector& AngularVelocity,
	const FVector& GroundNormal,
	float SettleStrength)
{
	const FVector Normal = GroundNormal.GetSafeNormal();
	if (SettleStrength <= 0.f || Normal.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	const FQuat Q = Orientation.Quaternion();
	const FVector BodyUp = Q.RotateVector(FVector::UpVector);

	// Settle against the face the drone landed on: aim its up axis at the ground normal when it is upright,
	// or at the anti-normal when it came to rest inverted. Choosing the nearer of the two keeps a flipped
	// drone flipped instead of driving it back upright, and bounds the correction to at most 90 degrees.
	const FVector TargetUp = (FVector::DotProduct(BodyUp, Normal) >= 0.f) ? Normal : -Normal;

	// The body-frame rotation that brings the drone's up axis onto the target. Built through a quaternion
	// and read back as an FRotator so its roll and pitch carry the same sign convention the integrator and
	// self-leveling use, rather than a hand-mapped axis-angle. Aligning the up axis never needs yaw, so the
	// correction's yaw comes out near zero.
	const FQuat WorldAlign = FQuat::FindBetweenNormals(BodyUp, TargetUp);
	const FRotator Correction = (Q.Inverse() * WorldAlign * Q).Rotator();

	// Critically damped pull toward the aligned attitude, damped by the matching body-axis rate so it
	// settles without ringing. The error is the negative correction so the spring torques toward the
	// target, exactly as self-leveling drives a Euler angle back to zero. Yaw is left to the flight model.
	const FVector Error(-Correction.Roll, -Correction.Pitch, 0.f);
	const FVector RollPitchRate(AngularVelocity.X, AngularVelocity.Y, 0.f);
	return CriticalSpring(Error, RollPitchRate, SettleStrength);
}
