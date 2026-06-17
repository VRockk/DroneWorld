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
