#include "Flight/FixedWingFlightModel.h"
#include "Flight/DroneAssist.h"

FDroneForces DroneFlight::ComputeFixedWingForces(
	const FDroneControlIntent& Intent,
	const FDroneFlightState& State,
	const FFixedWingFlightParams& Params)
{
	FDroneForces Out;

	const FVector BodyForward = State.Orientation.RotateVector(FVector::ForwardVector);
	const FVector BodyUp = State.Orientation.RotateVector(FVector::UpVector);

	// Only forward motion through the air makes lift; air hitting the wing backwards does not.
	const float ForwardAirspeed = FMath::Max(0.f, FVector::DotProduct(State.Velocity, BodyForward));

	// Lift grows with the square of airspeed and acts along body-up, so banking tilts it into a turn.
	// Below stall speed the wing cannot hold the air and lift collapses to nothing; above it, lift
	// saturates at MaxLift rather than growing without bound at high speed. Lift is a force, so a
	// heavier plane needs more airspeed before that lift holds it up.
	const float LiftMagnitude = (ForwardAirspeed >= Params.StallSpeed)
		? FMath::Min(Params.LiftCoefficient * ForwardAirspeed * ForwardAirspeed, Params.MaxLift)
		: 0.f;
	const FVector LiftForce = BodyUp * LiftMagnitude;

	// Throttle drives forward thrust along the nose, not lift. Thrust is a fixed force.
	const float ThrustMagnitude = FMath::Clamp(Intent.Throttle, 0.f, 1.f) * Params.MaxThrust;
	const FVector ThrustForce = BodyForward * ThrustMagnitude;

	// Gravity always pulls straight down. As a force it scales with mass, so its acceleration matches
	// any other airframe.
	const FVector GravityForce = FVector(0.f, 0.f, -Params.GravityAccel * Params.Mass);

	// Linear drag is an aerodynamic force opposing world velocity, independent of mass.
	const FVector DragForce = -State.Velocity * Params.DragCoefficient;

	Out.Force = LiftForce + ThrustForce + GravityForce + DragForce;

	// Stick demands are a feed-forward angular acceleration (Roll about X, Pitch about Y, Yaw about Z;
	// rolling banks the wing, which is what turns the aircraft), and rate damping bleeds off existing
	// rotation so a released stick coasts to a stop rather than spinning forever.
	Out.Torque = FVector(
		Intent.Roll * Params.ControlAuthority,
		Intent.Pitch * Params.ControlAuthority,
		Intent.Yaw * Params.ControlAuthority)
		- State.AngularVelocity * Params.AngularDamping;

	return Out;
}

FDroneForces DroneFlight::ComputeFixedWingHoverForces(
	const FDroneControlIntent& Intent,
	const FDroneFlightState& State,
	const FVector& HoldLocation,
	const FFixedWingFlightParams& Params)
{
	FDroneForces Out;

	// A plane cannot stop, so its hover is a loiter circle, not a point hold. Drive the maneuver
	// directly rather than leaning on airspeed lift (which collapses when slow and drops the plane):
	// cruise thrust along the nose, drag, and an altitude hold keep it flying level around the circle.
	const FVector BodyForward = State.Orientation.RotateVector(FVector::ForwardVector);
	const FVector Thrust = BodyForward * (Params.LoiterThrottle * Params.MaxThrust);
	const FVector Drag = -State.Velocity * Params.DragCoefficient;

	// Hold the engaged altitude: lift cancels gravity (so this carries no separate gravity term) and a
	// critically damped vertical spring pulls back to the held height, so the loiter never sinks. As a
	// hold controller it commands an acceleration, hence the mass scaling into a force.
	const float AltRestore = DroneFlight::CriticalSpring(
		State.Location.Z - HoldLocation.Z, State.Velocity.Z, Params.LoiterAltHoldGain);
	const FVector AltHold(0.f, 0.f, AltRestore * Params.Mass);

	Out.Force = Thrust + Drag + AltHold;

	// Bank to the loiter angle and hold pitch level (critically damped), and yaw at a steady turn rate
	// so the nose comes around the circle. Yaw tracks a rate, not an angle, hence the first-order term.
	Out.Torque = FVector(
		DroneFlight::CriticalSpring(State.Orientation.Roll - Params.LoiterBankAngle, State.AngularVelocity.X, Params.LoiterLevelStrength),
		DroneFlight::CriticalSpring(State.Orientation.Pitch, State.AngularVelocity.Y, Params.LoiterLevelStrength),
		(Params.LoiterTurnRate - State.AngularVelocity.Z) * Params.LoiterLevelStrength);

	return Out;
}

FDroneForces UFixedWingFlightModel::ComputeForces(const FDroneControlIntent& Intent, const FDroneFlightState& State) const
{
	return DroneFlight::ComputeFixedWingForces(Intent, State, Params);
}

FDroneForces UFixedWingFlightModel::ComputeHoverForces(const FDroneControlIntent& Intent, const FDroneFlightState& State, const FVector& HoldLocation) const
{
	return DroneFlight::ComputeFixedWingHoverForces(Intent, State, HoldLocation, Params);
}
