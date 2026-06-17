#include "Flight/DroneIntegrator.h"

FDroneFlightState DroneIntegrator::IntegrateStep(
	const FDroneFlightState& State,
	const FDroneForces& Forces,
	float Mass,
	float DeltaSeconds)
{
	FDroneFlightState Out = State;

	const float SafeMass = FMath::Max(Mass, KINDA_SMALL_NUMBER);

	// Linear: update velocity from acceleration, then advance position with the updated velocity.
	const FVector Acceleration = Forces.Force / SafeMass;
	Out.Velocity = State.Velocity + Acceleration * DeltaSeconds;
	Out.Location = State.Location + Out.Velocity * DeltaSeconds;

	// Angular: update angular velocity from angular acceleration, then advance orientation with it.
	Out.AngularVelocity = State.AngularVelocity + Forces.Torque * DeltaSeconds;

	// Apply the body-axis angular velocity (Roll about X, Pitch about Y, Yaw about Z) in the drone's
	// local frame, so a banked drone yaws about its own up axis rather than the world's.
	const FRotator DeltaRotation(
		Out.AngularVelocity.Y * DeltaSeconds,   // Pitch (about Y)
		Out.AngularVelocity.Z * DeltaSeconds,   // Yaw (about Z)
		Out.AngularVelocity.X * DeltaSeconds);  // Roll (about X)
	Out.Orientation = (State.Orientation.Quaternion() * DeltaRotation.Quaternion()).Rotator();

	return Out;
}
