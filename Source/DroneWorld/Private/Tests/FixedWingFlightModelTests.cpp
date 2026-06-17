#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/FixedWingFlightModel.h"

namespace
{
	// A flight state moving forward at a given airspeed, level and facing world +X, so body-forward
	// aligns with the velocity and body-up aligns with world up.
	FDroneFlightState ForwardFlight(float ForwardSpeed)
	{
		FDroneFlightState State;
		State.Velocity = FVector(ForwardSpeed, 0.f, 0.f);
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingLiftRisesWithAirspeed,
	"DroneWorld.Flight.FixedWing.LiftRisesWithAirspeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingLiftRisesWithAirspeed::RunTest(const FString& Parameters)
{
	// Above stall, a faster wing makes more lift, so the upward force grows with airspeed.
	const FDroneControlIntent Intent;
	FFixedWingFlightParams Params;

	// Both speeds sit in the rising region below the lift cap, so this isolates the airspeed->lift
	// relationship rather than the saturation handled by its own test.
	const float Slow = Params.StallSpeed * 1.5f;
	const float Fast = Params.StallSpeed * 2.5f;

	const float SlowLift = DroneFlight::ComputeFixedWingForces(Intent, ForwardFlight(Slow), Params).Force.Z;
	const float FastLift = DroneFlight::ComputeFixedWingForces(Intent, ForwardFlight(Fast), Params).Force.Z;

	TestTrue(TEXT("upward force is greater at higher airspeed"), FastLift > SlowLift);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingLiftSaturatesAtHighAirspeed,
	"DroneWorld.Flight.FixedWing.LiftSaturatesAtHighAirspeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingLiftSaturatesAtHighAirspeed::RunTest(const FString& Parameters)
{
	// The wing cannot make unbounded lift: well past the speed where uncapped lift would exceed the
	// cap, the lift force holds at MaxLift instead of running away.
	const FDroneControlIntent Intent;
	FFixedWingFlightParams Params;

	// An airspeed whose uncapped lift force (LiftCoefficient * v^2) far exceeds the cap.
	const float VeryFast = FMath::Sqrt(Params.MaxLift / Params.LiftCoefficient) * 3.f;

	// Level and flying straight, so Force.Z = LiftForce - Mass * GravityAccel with no vertical drag.
	const FDroneForces Forces = DroneFlight::ComputeFixedWingForces(Intent, ForwardFlight(VeryFast), Params);
	const float LiftForce = (float)Forces.Force.Z + Params.Mass * Params.GravityAccel;

	TestTrue(TEXT("lift force saturates at the cap"), FMath::IsNearlyEqual(LiftForce, Params.MaxLift, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingStallBelowMinSpeedLosesLift,
	"DroneWorld.Flight.FixedWing.StallBelowMinSpeedLosesLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingStallBelowMinSpeedLosesLift::RunTest(const FString& Parameters)
{
	// Below stall speed the wing loses lift, so gravity wins and the net force points down.
	const FDroneControlIntent Intent;
	FFixedWingFlightParams Params;

	const FDroneForces Stalled = DroneFlight::ComputeFixedWingForces(Intent, ForwardFlight(Params.StallSpeed * 0.5f), Params);

	TestTrue(TEXT("net force points downward below stall speed"), Stalled.Force.Z < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingThrottleProducesForwardThrust,
	"DroneWorld.Flight.FixedWing.ThrottleProducesForwardThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingThrottleProducesForwardThrust::RunTest(const FString& Parameters)
{
	// Throttle drives forward thrust along body-forward, unlike the quad where it lifts.
	const FDroneFlightState State;
	FFixedWingFlightParams Params;

	FDroneControlIntent FullThrottle;
	FullThrottle.Throttle = 1.f;

	const float Idle = DroneFlight::ComputeFixedWingForces(FDroneControlIntent(), State, Params).Force.X;
	const float Powered = DroneFlight::ComputeFixedWingForces(FullThrottle, State, Params).Force.X;

	TestTrue(TEXT("throttle adds forward force"), Powered > Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingStickIntentProducesExpectedTorqueSign,
	"DroneWorld.Flight.FixedWing.StickIntentProducesExpectedTorqueSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingStickIntentProducesExpectedTorqueSign::RunTest(const FString& Parameters)
{
	// Positive roll/pitch/yaw stick produces positive angular acceleration about X/Y/Z respectively;
	// banking is what turns a fixed wing, so roll authority is the primary turn control.
	const FDroneFlightState State;
	FFixedWingFlightParams Params;

	FDroneControlIntent Roll;
	Roll.Roll = 1.f;
	TestTrue(TEXT("positive roll -> positive X torque"), DroneFlight::ComputeFixedWingForces(Roll, State, Params).Torque.X > 0.f);

	FDroneControlIntent Pitch;
	Pitch.Pitch = 1.f;
	TestTrue(TEXT("positive pitch -> positive Y torque"), DroneFlight::ComputeFixedWingForces(Pitch, State, Params).Torque.Y > 0.f);

	FDroneControlIntent Yaw;
	Yaw.Yaw = 1.f;
	TestTrue(TEXT("positive yaw -> positive Z torque"), DroneFlight::ComputeFixedWingForces(Yaw, State, Params).Torque.Z > 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingHeavierAcceleratesLess,
	"DroneWorld.Flight.FixedWing.HeavierAcceleratesLess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingHeavierAcceleratesLess::RunTest(const FString& Parameters)
{
	// Weight matters: with identical throttle, a heavier aircraft gains less forward acceleration,
	// because thrust is a fixed force and acceleration is force over mass.
	FDroneControlIntent FullThrottle;
	FullThrottle.Throttle = 1.f;
	const FDroneFlightState State;

	FFixedWingFlightParams Light;
	Light.Mass = 2.f;
	FFixedWingFlightParams Heavy;
	Heavy.Mass = 20.f;

	const float LightAccel = (float)(DroneFlight::ComputeFixedWingForces(FullThrottle, State, Light).Force.X / Light.Mass);
	const float HeavyAccel = (float)(DroneFlight::ComputeFixedWingForces(FullThrottle, State, Heavy).Force.X / Heavy.Mass);

	TestTrue(TEXT("the lighter aircraft accelerates harder than the heavier one at the same throttle"), LightAccel > HeavyAccel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightFixedWingAngularRateDampsToStop,
	"DroneWorld.Flight.FixedWing.AngularRateDampsToStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightFixedWingAngularRateDampsToStop::RunTest(const FString& Parameters)
{
	// With the sticks centered, a rotating aircraft is damped on every axis: the torque opposes the
	// existing angular velocity so rotation bleeds off and the aircraft stops rather than coasting.
	const FDroneControlIntent Centered;
	const FFixedWingFlightParams Params;

	FDroneFlightState Spinning;
	Spinning.AngularVelocity = FVector(40.f, -30.f, 60.f);

	const FDroneForces Forces = DroneFlight::ComputeFixedWingForces(Centered, Spinning, Params);

	TestTrue(TEXT("positive roll rate is damped (negative roll torque)"), Forces.Torque.X < 0.f);
	TestTrue(TEXT("negative pitch rate is damped (positive pitch torque)"), Forces.Torque.Y > 0.f);
	TestTrue(TEXT("positive yaw rate is damped (negative yaw torque)"), Forces.Torque.Z < 0.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
