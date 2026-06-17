#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/QuadFlightModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightQuadZeroThrottleNetsDownward,
	"DroneWorld.Flight.Quad.ZeroThrottleNetsDownward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightQuadZeroThrottleNetsDownward::RunTest(const FString& Parameters)
{
	// A level quad at rest with no throttle: gravity dominates, so the net force is downward.
	const FDroneControlIntent Intent;
	const FDroneFlightState State;
	const FQuadFlightParams Params;

	const FDroneForces Forces = DroneFlight::ComputeQuadForces(Intent, State, Params);

	TestTrue(TEXT("net force points downward at zero throttle"), Forces.Force.Z < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightQuadCollectiveProducesLift,
	"DroneWorld.Flight.Quad.CollectiveProducesLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightQuadCollectiveProducesLift::RunTest(const FString& Parameters)
{
	// A level quad at full collective: thrust exceeds gravity, so the net force is upward.
	FDroneControlIntent Intent;
	Intent.Throttle = 1.f;
	const FDroneFlightState State;
	const FQuadFlightParams Params;

	const FDroneForces Forces = DroneFlight::ComputeQuadForces(Intent, State, Params);

	TestTrue(TEXT("default thrust force exceeds the drone's weight"), Params.MaxThrust > Params.Mass * Params.GravityAccel);
	TestTrue(TEXT("net force points upward at full throttle"), Forces.Force.Z > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightQuadStickIntentProducesExpectedTorqueSign,
	"DroneWorld.Flight.Quad.StickIntentProducesExpectedTorqueSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightQuadStickIntentProducesExpectedTorqueSign::RunTest(const FString& Parameters)
{
	// Positive yaw/pitch/roll stick produces positive angular acceleration about Z/Y/X respectively.
	const FDroneFlightState State;
	const FQuadFlightParams Params;

	FDroneControlIntent Roll;
	Roll.Roll = 1.f;
	TestTrue(TEXT("positive roll -> positive X torque"), DroneFlight::ComputeQuadForces(Roll, State, Params).Torque.X > 0.f);

	FDroneControlIntent Pitch;
	Pitch.Pitch = 1.f;
	TestTrue(TEXT("positive pitch -> positive Y torque"), DroneFlight::ComputeQuadForces(Pitch, State, Params).Torque.Y > 0.f);

	FDroneControlIntent Yaw;
	Yaw.Yaw = 1.f;
	TestTrue(TEXT("positive yaw -> positive Z torque"), DroneFlight::ComputeQuadForces(Yaw, State, Params).Torque.Z > 0.f);

	// A centered stick produces no rotation.
	const FDroneControlIntent Centered;
	TestTrue(TEXT("centered sticks -> zero torque"), DroneFlight::ComputeQuadForces(Centered, State, Params).Torque.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightQuadHeavierAcceleratesLess,
	"DroneWorld.Flight.Quad.HeavierAcceleratesLess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightQuadHeavierAcceleratesLess::RunTest(const FString& Parameters)
{
	// Weight matters: with identical thrust, a heavier drone gains less acceleration than a light one,
	// because thrust is a fixed force and acceleration is force over mass.
	FDroneControlIntent FullThrottle;
	FullThrottle.Throttle = 1.f;
	const FDroneFlightState State;

	FQuadFlightParams Light;
	Light.Mass = 2.f;
	FQuadFlightParams Heavy;
	Heavy.Mass = 20.f;

	const float LightAccel = (float)(DroneFlight::ComputeQuadForces(FullThrottle, State, Light).Force.Z / Light.Mass);
	const float HeavyAccel = (float)(DroneFlight::ComputeQuadForces(FullThrottle, State, Heavy).Force.Z / Heavy.Mass);

	TestTrue(TEXT("the lighter drone accelerates harder than the heavier one at the same thrust"), LightAccel > HeavyAccel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneFlightQuadAngularRateDampsToStop,
	"DroneWorld.Flight.Quad.AngularRateDampsToStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneFlightQuadAngularRateDampsToStop::RunTest(const FString& Parameters)
{
	// With the sticks centered, a rotating drone is damped on every axis: the torque opposes the
	// existing angular velocity, so rotation bleeds off and the drone stops rather than coasting
	// forever. Yaw in particular has no leveling to lean on, so this is its only way to stop.
	const FDroneControlIntent Centered;
	const FQuadFlightParams Params;

	FDroneFlightState Spinning;
	Spinning.AngularVelocity = FVector(40.f, -30.f, 60.f);

	const FDroneForces Forces = DroneFlight::ComputeQuadForces(Centered, Spinning, Params);

	TestTrue(TEXT("positive roll rate is damped (negative roll torque)"), Forces.Torque.X < 0.f);
	TestTrue(TEXT("negative pitch rate is damped (positive pitch torque)"), Forces.Torque.Y > 0.f);
	TestTrue(TEXT("positive yaw rate is damped (negative yaw torque)"), Forces.Torque.Z < 0.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
