#include "Flight/QuadFlightModel.h"
#include "Flight/DroneAssist.h"

FDroneForces DroneFlight::ComputeQuadForces(
	const FDroneControlIntent& Intent,
	const FDroneFlightState& State,
	const FQuadFlightParams& Params)
{
	FDroneForces Out;

	// Collective thrust acts along the drone's body-up axis, so banking redirects lift. Thrust is a
	// fixed force, so a heavier drone gets less acceleration from it.
	const FVector BodyUp = State.Orientation.RotateVector(FVector::UpVector);
	const float ThrustMagnitude = FMath::Clamp(Intent.Throttle, 0.f, 1.f) * Params.MaxThrust;
	const FVector ThrustForce = BodyUp * ThrustMagnitude;

	// Gravity always pulls straight down. As a force it scales with mass, so its acceleration is the
	// same for any drone (everything falls at GravityAccel).
	const FVector GravityForce = FVector(0.f, 0.f, -DroneFlight::GravityAccel * Params.Mass);

	// Linear drag is an aerodynamic force opposing world velocity, independent of mass.
	const FVector DragForce = -State.Velocity * Params.DragCoefficient;

	Out.Force = ThrustForce + GravityForce + DragForce;

	// Stick demands are a feed-forward angular acceleration (Roll about X, Pitch about Y, Yaw about Z),
	// and rate damping bleeds off existing rotation so a released stick coasts to a stop rather than
	// spinning forever. The two together settle to a steady rate while a stick is held.
	Out.Torque = FVector(
		Intent.Roll * Params.ControlAuthority,
		Intent.Pitch * Params.ControlAuthority,
		Intent.Yaw * Params.ControlAuthority)
		- State.AngularVelocity * Params.AngularDamping;

	return Out;
}

FDroneForces DroneFlight::ComputeQuadHoverForces(
	const FDroneControlIntent& Intent,
	const FDroneFlightState& State,
	const FVector& HoldLocation,
	const FQuadFlightParams& Params)
{
	FDroneForces Out;

	// A multirotor can stop in place, so hover parks it on the hold point. Pull back toward that point
	// with a critically damped spring on the position offset, in all three axes at once, so the drone
	// holds both its horizontal spot and its altitude.
	const FVector Offset = State.Location - HoldLocation;
	const FVector RestoreAccel = DroneFlight::CriticalSpring(Offset, State.Velocity, Params.HoverPositionGain);

	// Cancel gravity so the hold neither sinks nor climbs when sitting on the setpoint.
	const FVector GravityComp(0.f, 0.f, DroneFlight::GravityAccel);
	Out.Force = (RestoreAccel + GravityComp) * Params.Mass;

	// Hold the airframe flat: a critically damped pull of roll and pitch back to level, with a zero
	// yaw error so yaw is purely damped and the drone keeps its heading rather than spinning.
	Out.Torque = DroneFlight::CriticalSpring(
		FVector(State.Orientation.Roll, State.Orientation.Pitch, 0.f),
		State.AngularVelocity,
		Params.HoverLevelStrength);

	return Out;
}

FDroneForces UQuadFlightModel::ComputeForces(const FDroneControlIntent& Intent, const FDroneFlightState& State) const
{
	return DroneFlight::ComputeQuadForces(Intent, State, Params);
}

FDroneForces UQuadFlightModel::ComputeHoverForces(const FDroneControlIntent& Intent, const FDroneFlightState& State, const FVector& HoldLocation) const
{
	return DroneFlight::ComputeQuadHoverForces(Intent, State, HoldLocation, Params);
}
